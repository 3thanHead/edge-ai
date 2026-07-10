"""Free, no-key job sources for the reverse-ATS matcher.

Each source is an async `fetch(query, limit) -> list[job]` that returns the
same normalized shape, so the ingest loop treats them uniformly. Add a source
= add a function and list it in SOURCES.

    {source, external_id, url, title, company, location, remote,
     description, posted_at}

Both sources below are public JSON APIs that need no key:
  - Remotive     https://remotive.com/api/remote-jobs   (server-side search)
  - Arbeitnow    https://www.arbeitnow.com/api/job-board-api  (filtered here)
"""
import logging
import re

import httpx

log = logging.getLogger("agents.jobs.sources")

_TIMEOUT = httpx.Timeout(20.0)
_HEADERS = {"User-Agent": "iot_ai-agents/0.1 (reverse-ats job matcher)"}
_MAX_DESC = 2000  # store enough for scoring, not the whole posting

_TAG = re.compile(r"<[^>]+>")


def _text(html: str) -> str:
    """Strip HTML + collapse whitespace; postings come as HTML descriptions."""
    return re.sub(r"\s+", " ", _TAG.sub(" ", html or "")).strip()[:_MAX_DESC]


async def _get(url: str, params: dict | None = None):
    async with httpx.AsyncClient(timeout=_TIMEOUT, headers=_HEADERS) as client:
        resp = await client.get(url, params=params)
        resp.raise_for_status()
        return resp.json()


async def remotive(query: str, limit: int = 40) -> list[dict]:
    data = await _get("https://remotive.com/api/remote-jobs",
                      {"search": query, "limit": limit})
    jobs = []
    for j in (data.get("jobs") or [])[:limit]:
        jobs.append({
            "source": "remotive",
            "external_id": str(j.get("id") or ""),
            "url": j.get("url"),
            "title": j.get("title"),
            "company": j.get("company_name"),
            "location": j.get("candidate_required_location") or "",
            "remote": True,
            "description": _text(j.get("description")),
            "posted_at": j.get("publication_date"),
        })
    return jobs


async def arbeitnow(query: str, limit: int = 40) -> list[dict]:
    # No server-side search on this endpoint; pull the board and filter by the
    # query terms against title/description/tags.
    data = await _get("https://www.arbeitnow.com/api/job-board-api")
    terms = [t for t in query.lower().split() if len(t) > 2]
    jobs = []
    for j in (data.get("data") or []):
        hay = (f"{j.get('title', '')} {j.get('description', '')} "
               f"{' '.join(j.get('tags') or [])}").lower()
        if terms and not any(t in hay for t in terms):
            continue
        jobs.append({
            "source": "arbeitnow",
            "external_id": j.get("slug") or "",
            "url": j.get("url"),
            "title": j.get("title"),
            "company": j.get("company_name"),
            "location": j.get("location") or "",
            "remote": bool(j.get("remote")),
            "description": _text(j.get("description")),
            "posted_at": None,
        })
        if len(jobs) >= limit:
            break
    return jobs


SOURCES = [remotive, arbeitnow]


async def fetch_all(query: str, limit: int = 40) -> list[dict]:
    """Fetch from every source concurrently; a source that errors is logged
    and skipped so one bad API doesn't stall the loop. Deduped by URL."""
    import asyncio
    results = await asyncio.gather(*[s(query, limit) for s in SOURCES],
                                   return_exceptions=True)
    out, seen = [], set()
    for src, res in zip(SOURCES, results):
        if isinstance(res, Exception):
            log.warning("source %s failed: %s", getattr(src, "__name__", src), res)
            continue
        for job in res:
            url = job.get("url")
            if url and url not in seen:
                seen.add(url)
                out.append(job)
    return out
