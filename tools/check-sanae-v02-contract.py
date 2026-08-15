#!/usr/bin/env python3
"""Static client/OpenAPI conformance checks for Sanae v0.2.

Usage: python tools/check-sanae-v02-contract.py /path/to/sanae-server-openapi-v0.2.json
"""

from __future__ import annotations

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def ref_name(schema: dict) -> str:
    return schema["$ref"].rsplit("/", 1)[-1]


def main() -> int:
    require(len(sys.argv) == 2, "pass the authoritative Sanae v0.2 OpenAPI JSON path")
    openapi_path = pathlib.Path(sys.argv[1])
    document = json.loads(openapi_path.read_text(encoding="utf-8"))
    paths = document["paths"]
    schemas = document["components"]["schemas"]

    routes = {
        ("post", "/api/v1/auth/enroll"),
        ("get", "/api/v1/me"),
        ("get", "/api/v1/seasons"),
        ("post", "/api/v1/seasons"),
        ("get", "/api/v1/projects"),
        ("post", "/api/v1/projects"),
        ("post", "/api/v1/projects/{project_id}/sync"),
        ("get", "/api/v1/projects/{project_id}/terminology/history"),
        ("post", "/api/v1/projects/{project_id}/episodes"),
        ("get", "/api/v1/episodes/{episode_id}"),
        ("delete", "/api/v1/episodes/{episode_id}"),
        ("post", "/api/v1/episodes/{episode_id}/source"),
        ("get", "/api/v1/files/{file_id}"),
        ("post", "/api/v1/episodes/{episode_id}/finalize"),
        ("post", "/api/v1/episodes/{episode_id}/recovery-snapshots"),
        ("get", "/api/v1/episodes/{episode_id}/recovery-snapshots"),
        ("get", "/api/v1/recovery-snapshots/{snapshot_id}/file"),
        ("delete", "/api/v1/recovery-snapshots/{snapshot_id}"),
    }
    for method, path in routes:
        require(path in paths and method in paths[path], f"missing authoritative route: {method.upper()} {path}")

    season_create = paths["/api/v1/seasons"]["post"]
    season_request = schemas[ref_name(season_create["requestBody"]["content"]["application/json"]["schema"])]
    require({"year", "code", "display_name"} <= set(season_request["required"]),
            "season creation fields changed")
    project_create = paths["/api/v1/projects"]["post"]
    project_request = schemas[ref_name(project_create["requestBody"]["content"]["application/json"]["schema"])]
    require({"season_id", "slug", "name"} <= set(project_request["required"]),
            "project creation fields changed")

    sync_op = paths["/api/v1/projects/{project_id}/sync"]["post"]
    sync_request = schemas[ref_name(sync_op["requestBody"]["content"]["application/json"]["schema"])]
    require(sync_request["required"] == ["since_revision"], "sync request fields changed")
    sync_response_ref = sync_op["responses"]["200"]["content"]["application/json"]["schema"]
    sync_required = set(schemas[ref_name(sync_response_ref)]["required"])
    require(
        {
            "from_revision", "to_revision", "full_snapshot", "project", "changes",
            "episodes", "files", "finalized_revisions", "terminology",
            "terminology_history", "ignored_candidates",
        }
        <= sync_required,
        "sync response fields changed",
    )

    episode_op = paths["/api/v1/projects/{project_id}/episodes"]["post"]
    episode_body = schemas[ref_name(episode_op["requestBody"]["content"]["multipart/form-data"]["schema"])]
    require(set(episode_body["required"]) == {"metadata", "ensub"}, "episode multipart fields changed")

    # Episode maintenance added for v0.2 human-error recovery.
    source_op = paths["/api/v1/episodes/{episode_id}/source"]["post"]
    source_body = schemas[ref_name(source_op["requestBody"]["content"]["multipart/form-data"]["schema"])]
    require(set(source_body["required"]) == {"metadata", "ensub"}, "source replacement multipart fields changed")
    require(schemas["EpisodeSourceMetadata"]["required"] == ["base_source_file_id"],
            "source replacement concurrency field changed")
    source_response = schemas["EpisodeSourceResponse"]
    require({"episode", "source_file", "project_revision"} == set(source_response["required"]),
            "source replacement response fields changed")
    delete_response = schemas["EpisodeDeleteResponse"]
    require({"episode", "project_revision"} == set(delete_response["required"]),
            "episode deletion response fields changed")
    episode_read = schemas["EpisodeReadResponse"]
    require({"episode", "files", "finalized_revisions"} == set(episode_read["required"]),
            "episode inspection response fields changed")
    require("deleted_at" in schemas["EpisodeBody"]["required"], "episode tombstone field changed")

    finalize_op = paths["/api/v1/episodes/{episode_id}/finalize"]["post"]
    finalize_body = schemas[ref_name(finalize_op["requestBody"]["content"]["multipart/form-data"]["schema"])]
    require(set(finalize_body["required"]) == {"metadata", "compact_rusub"}, "Finalize multipart fields changed")
    finalize_metadata = schemas["FinalizeMetadata"]
    require(
        {"base_project_revision", "source_file_id"} <= set(finalize_metadata["required"]),
        "Finalize concurrency fields changed",
    )
    require(
        {"base_project_revision", "source_file_id", "base_finalized_revision_id", "terminology_ops", "ignore_ops"}
        <= set(finalize_metadata["properties"]),
        "Finalize metadata fields changed",
    )
    require(finalize_metadata["properties"]["terminology_ops"]["maxItems"] == 1000,
            "Finalize terminology operation limit changed")

    api_source = (ROOT / "src/sanae_project.cpp").read_text(encoding="utf-8")
    required_client_fragments = {
        '"/api/v1/auth/enroll"',
        '"/api/v1/seasons"',
        '"/api/v1/projects"',
        '"/sync"',
        '"/episodes"',
        '"/api/v1/files/"',
        '"/finalize"',
        '"/source"',
        'metadata["base_source_file_id"]',
        'metadata["base_project_revision"]',
        'metadata["source_file_id"]',
        'metadata["base_finalized_revision_id"]',
        'metadata["terminology_ops"]',
        'metadata["ignore_ops"]',
        '"ensub"',
        '"compact_rusub"',
    }
    for fragment in required_client_fragments:
        require(fragment in api_source, f"client contract fragment missing: {fragment}")
    api_transport = (ROOT / "src/sanae_api.cpp").read_text(encoding="utf-8")
    require('Perform("DELETE"' in api_transport,
            "client transport does not implement authenticated DELETE")
    api_literals = set(re.findall(r'"(/api/v1[^"\\]*)"', api_source))
    require(
        api_literals
        <= {
            "/api/v1/auth/enroll", "/api/v1/me", "/api/v1/seasons", "/api/v1/projects",
            "/api/v1/projects/", "/api/v1/files/", "/api/v1/episodes/",
            "/api/v1/recovery-snapshots/",
        },
        f"client contains an unrecognized API path literal: {sorted(api_literals)}",
    )

    config = json.loads((ROOT / "src/libresrc/default_config.json").read_text(encoding="utf-8"))
    require(config["Sanae"]["Server"]["Base URL"] == "https://sanae.webredirect.org",
            "default server URL changed")
    require(config["Sanae"]["Project"]["Source Repeat"]["Similar Threshold"] >= 0.9,
            "default similar threshold is no longer high")

    meson_sources = (ROOT / "src/meson.build").read_text(encoding="utf-8")
    for source in (
        "command/sanae.cpp", "dialog_sanae_connection.cpp", "dialog_sanae_final_review.cpp",
        "dialog_sanae_episode.cpp", "dialog_sanae_project.cpp", "dialog_sanae_terminology.cpp",
        "sanae_api.cpp", "sanae_compact_rusub.cpp", "sanae_project.cpp",
        "sanae_subtitle_diff.cpp", "sanae_text.cpp",
    ):
        require(f"'{source}'" in meson_sources, f"Meson source missing: {source}")

    print("Sanae v0.2 client/OpenAPI static contract checks passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, KeyError, json.JSONDecodeError) as error:
        print(f"contract check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
