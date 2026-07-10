"""jobs -- a reverse-ATS job finder driven by your resume.

Give it your resume in chat and it does the ATS match in reverse: instead of a
recruiter's ATS scoring your resume against a job, it scores every job it finds
against *your* resume -- how well you'd pass that posting's keyword screen. It
keeps a background LangGraph loop running that pulls postings from free sources
and scores each new one, so matches accumulate in the shared JSON store.

How the agent reads a chat message (run() is overridden -- this agent controls
a worker instead of doing a one-shot tool loop):

    <a resume>      extract an ATS profile (titles/skills/seniority) with the
                    cluster LLM, then (re)start the ingest loop for it
    top [N]         the N best-scoring jobs found so far
    status          is the loop running, how many jobs / strong matches
    stop            stop the loop

The loop itself is a LangGraph cycle -- fetch -> score -> store -- repeated on
an interval by a small supervisor task (IngestManager). LangGraph models the
per-cycle data flow; the manager owns repetition, the interval, and stop.

Stored per job (collection "jobs" in app/db.py), queryable by property:
    {source, url, title, company, location, remote, description, posted_at,
     match_score: 0-100, matched_keywords: [...], missing_keywords: [...],
     verdict: "strong"|"partial"|"weak", summary: "...", matched_at}
"""
import asyncio
import json
import logging
import os
from datetime import datetime, timezone
from typing import TypedDict

from langchain_core.messages import HumanMessage, SystemMessage
from langgraph.graph import END, StateGraph

from .. import db, jobs_sources
from . import events
from .base import BaseAgent, _extract_json

log = logging.getLogger("agents.jobs")

COLLECTION = "jobs"
INGEST_INTERVAL = int(os.environ.get("JOBS_INGEST_INTERVAL", "600"))  # seconds
MAX_PER_CYCLE = int(os.environ.get("JOBS_MAX_PER_CYCLE", "12"))       # LLM calls/cycle

PROFILE_SYS = (
    "You extract an ATS profile from a resume. An ATS keys on job titles, hard "
    "skills/technologies, and seniority. Reply with ONLY this JSON:\n"
    '{"titles": ["<up to 3 target job titles>"], '
    '"skills": ["<key technologies/skills an ATS would match>"], '
    '"seniority": "junior|mid|senior|lead", '
    '"years_experience": <integer>, '
    '"location_pref": "remote|onsite|hybrid|<city>"}\n'
    "Titles should be role names you could search a job board with. No prose."
)

SCORE_SYS = (
    "You are a reverse-ATS matcher. Given a CANDIDATE PROFILE and ONE JOB, "
    "score how well this candidate's resume would pass THIS job's ATS keyword "
    "screen -- i.e. what fraction of the job's required skills/keywords the "
    "candidate has. Reply with ONLY this JSON:\n"
    '{"match_score": <0-100 integer>, '
    '"matched_keywords": ["<job requirements the candidate HAS>"], '
    '"missing_keywords": ["<job requirements the candidate LACKS>"], '
    '"verdict": "strong|partial|weak", '
    '"summary": "<one sentence: why this score>"}\n'
    "Base the score on requirement coverage, not enthusiasm. No prose."
)


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _verdict(score: int) -> str:
    return "strong" if score >= 75 else "partial" if score >= 45 else "weak"


def _queries_from_profile(profile: dict) -> list[str]:
    """Search terms to hit the job boards with: the target titles, plus the
    top couple of skills as a fallback. Deduped, capped."""
    queries: list[str] = []
    for t in (profile.get("titles") or []):
        if t and t.lower() not in [q.lower() for q in queries]:
            queries.append(t)
    for s in (profile.get("skills") or [])[:2]:
        if s and s.lower() not in [q.lower() for q in queries]:
            queries.append(s)
    return queries[:4] or ["software engineer"]


class CycleState(TypedDict):
    profile: dict
    query: str
    jobs: list
    scored: list
    stored: int


class IngestManager:
    """Owns the single background ingest loop. A new resume replaces the
    running loop; stop() cancels it. In-memory (the loop is a task, not
    persisted) -- the collected jobs live in the DB and outlast it."""

    def __init__(self) -> None:
        self._task: asyncio.Task | None = None
        self._stop: asyncio.Event | None = None
        self.profile: dict = {}
        self.stats: dict = {}

    def running(self) -> bool:
        return self._task is not None and not self._task.done()

    async def start(self, profile: dict, queries: list[str], agent: "JobsAgent") -> None:
        await self.stop()
        self._stop = asyncio.Event()
        self.profile = profile
        self.stats = {"queries": queries, "cycles": 0, "found": 0, "stored": 0,
                      "started_at": _now_iso()}
        self._task = asyncio.create_task(self._loop(profile, queries, agent))

    async def stop(self) -> None:
        if self._stop:
            self._stop.set()
        if self._task and not self._task.done():
            try:
                await asyncio.wait_for(self._task, timeout=5)
            except (asyncio.TimeoutError, asyncio.CancelledError):
                self._task.cancel()
        self._task = None
        self._stop = None

    async def _loop(self, profile: dict, queries: list[str], agent: "JobsAgent") -> None:
        assert self._stop is not None
        while not self._stop.is_set():
            try:
                await agent.ingest_cycle(profile, queries, self.stats, self._stop)
            except Exception:  # never let a cycle kill the loop
                log.exception("ingest cycle failed")
            self.stats["cycles"] += 1
            # Interruptible sleep: wake early when stopped.
            try:
                await asyncio.wait_for(self._stop.wait(), timeout=INGEST_INTERVAL)
            except asyncio.TimeoutError:
                pass


manager = IngestManager()


class JobsAgent(BaseAgent):
    name = "jobs"
    description = ("Reverse-ATS job finder: paste your resume and it keeps "
                   "pulling jobs from free sources and scoring how well you'd "
                   "match each one. Then ask 'top 10', 'status', or 'stop'.")

    # run() is overridden, so the tool-loop hooks below go unused; they only
    # satisfy BaseAgent's abstract interface.
    def system_prompt(self) -> str:
        return ""

    def tools(self):
        return []

    # -- LLM steps ---------------------------------------------------------

    async def _llm_json(self, system: str, user: str) -> dict:
        ai = await self.llm().ainvoke([SystemMessage(system), HumanMessage(user)])
        return _extract_json(str(ai.content)) or {}

    async def extract_profile(self, resume: str) -> dict:
        p = await self._llm_json(PROFILE_SYS, resume[:6000])
        return {
            "titles": [str(t) for t in (p.get("titles") or [])][:3],
            "skills": [str(s) for s in (p.get("skills") or [])][:20],
            "seniority": str(p.get("seniority") or ""),
            "years_experience": p.get("years_experience"),
            "location_pref": str(p.get("location_pref") or ""),
        }

    async def score_job(self, profile: dict, job: dict) -> dict:
        user = (
            "CANDIDATE PROFILE:\n"
            + json.dumps({"titles": profile.get("titles"),
                          "skills": profile.get("skills"),
                          "seniority": profile.get("seniority")})
            + "\n\nJOB:\n"
            f"Title: {job.get('title')}\nCompany: {job.get('company')}\n"
            f"Description:\n{job.get('description') or ''}"
        )
        m = await self._llm_json(SCORE_SYS, user)
        try:
            score = max(0, min(100, int(m.get("match_score"))))
        except (TypeError, ValueError):
            score = 0
        verdict = m.get("verdict")
        return {
            "match_score": score,
            "matched_keywords": [str(k) for k in (m.get("matched_keywords") or [])][:12],
            "missing_keywords": [str(k) for k in (m.get("missing_keywords") or [])][:12],
            "verdict": verdict if verdict in ("strong", "partial", "weak") else _verdict(score),
            "summary": str(m.get("summary") or "")[:300],
        }

    # -- the LangGraph ingest cycle ---------------------------------------

    def _graph(self):
        """fetch -> score -> store, compiled once and reused each cycle."""
        if getattr(self, "_compiled", None) is not None:
            return self._compiled

        async def fetch(state: CycleState) -> dict:
            found = await jobs_sources.fetch_all(state["query"], limit=40)
            fresh = []
            for job in found:
                url = job.get("url")
                if url and not await db.exists(COLLECTION, url):
                    fresh.append(job)
                if len(fresh) >= MAX_PER_CYCLE:
                    break
            return {"jobs": fresh}

        async def score(state: CycleState) -> dict:
            scored = []
            for job in state["jobs"]:
                try:
                    match = await self.score_job(state["profile"], job)
                except Exception:
                    log.exception("scoring failed for %s", job.get("url"))
                    continue
                scored.append({**job, **match, "matched_at": _now_iso()})
            return {"scored": scored}

        async def store(state: CycleState) -> dict:
            n = 0
            for job in state["scored"]:
                if await db.insert(COLLECTION, job, dedup_key=job.get("url")):
                    n += 1
            return {"stored": n}

        g = StateGraph(CycleState)
        g.add_node("fetch", fetch)
        g.add_node("score", score)
        g.add_node("store", store)
        g.set_entry_point("fetch")
        g.add_edge("fetch", "score")
        g.add_edge("score", "store")
        g.add_edge("store", END)
        self._compiled = g.compile()
        return self._compiled

    async def ingest_cycle(self, profile: dict, queries: list[str],
                           stats: dict, stop: asyncio.Event) -> None:
        graph = self._graph()
        for query in queries:
            if stop.is_set():
                return
            result = await graph.ainvoke({
                "profile": profile, "query": query,
                "jobs": [], "scored": [], "stored": 0,
            })
            stats["found"] += len(result.get("scored", []))
            stats["stored"] += result.get("stored", 0)
            log.info("cycle query=%r scored=%d stored=%d", query,
                     len(result.get("scored", [])), result.get("stored", 0))

    # -- chat entry point --------------------------------------------------

    async def run(self, input_text: str):
        yield events.start(self.name, input_text[:200])
        cmd = input_text.strip()
        low = cmd.lower()
        try:
            if low in ("stop", "cancel", "halt", "pause"):
                await manager.stop()
                yield events.final({"status": "stopped",
                                    "message": "Stopped the job search."})
                return
            if low in ("status", "stats", "state"):
                yield events.final(await self._status())
                return
            if low.startswith("top") or low.startswith("best"):
                n = _first_int(low, default=10)
                yield events.final(await self._top(n))
                return

            resume = await self._maybe_read_file(cmd) or cmd
            if len(resume) < 120:
                yield events.final({
                    "status": "idle",
                    "message": ("Paste your resume text to start matching, or "
                                "ask 'top 10' / 'status' / 'stop'."),
                })
                return

            yield events.thinking("Reading your resume and extracting an ATS profile…")
            yield events.tool_call("extract_profile", {"resume_chars": len(resume)})
            profile = await self.extract_profile(resume)
            yield events.tool_result("extract_profile", profile)

            queries = _queries_from_profile(profile)
            await manager.start(profile, queries, self)
            titles = ", ".join(profile.get("titles") or []) or "your profile"
            yield events.final({
                "status": "ingesting",
                "message": (f"Matching jobs against your resume ({titles}). "
                            f"I'll keep pulling and scoring postings in the "
                            f"background — ask 'top 10' or 'status' anytime."),
                "profile": profile,
                "queries": queries,
            })
        except Exception as e:
            log.exception("jobs agent failed")
            yield events.error(str(e))

    async def _status(self) -> dict:
        total = await db.count(COLLECTION)
        strong = await db.count(COLLECTION, where={"verdict": "strong"})
        running = manager.running()
        return {
            "status": "ingesting" if running else "idle",
            "message": (f"{'Running' if running else 'Idle'}. {total} jobs "
                        f"collected, {strong} strong matches."),
            "jobs_total": total,
            "strong_matches": strong,
            "queries": manager.stats.get("queries", []) if running else [],
            "cycles": manager.stats.get("cycles", 0),
        }

    async def _top(self, n: int) -> dict:
        n = max(1, min(50, n))
        jobs = await db.find(COLLECTION, order_by="match_score", desc=True, limit=n)
        matches = [{
            "title": j.get("title"),
            "company": j.get("company"),
            "url": j.get("url"),
            "score": j.get("match_score"),
            "verdict": j.get("verdict"),
            "missing": (j.get("missing_keywords") or [])[:5],
            "summary": j.get("summary"),
        } for j in jobs]
        msg = (f"Top {len(matches)} matches for your resume."
               if matches else "No jobs scored yet — paste your resume to start.")
        return {"status": "ok", "message": msg, "matches": matches}

    async def _maybe_read_file(self, text: str) -> str | None:
        """Let the user pass a path to a resume file (e.g. one on the mounted
        /data volume) instead of pasting it."""
        p = text.strip()
        if len(p) < 300 and "\n" not in p and (
                p.startswith("/") or p.lower().endswith((".txt", ".md"))):
            try:
                if os.path.isfile(p):
                    with open(p, encoding="utf-8", errors="ignore") as f:
                        return f.read()
            except OSError:
                pass
        return None


def _first_int(text: str, default: int) -> int:
    import re
    m = re.search(r"\d+", text)
    return int(m.group()) if m else default


AGENT = JobsAgent()
