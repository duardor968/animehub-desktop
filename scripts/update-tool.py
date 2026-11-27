#!/usr/bin/env python3
"""AnimeHub Desktop updater packaging & Ed25519 manifest signing CLI.

Subcommands:
  pack-win64     Builds sorted zstd-19 .ahpack delta packfile, writes installed-manifest.json
                 into the stage directory, and emits Ed25519-signed update-win64.json (when
                 --out-dir / signing key are provided) or a standalone .ahpack + .index.json
                 sidecar (when --source/--output are used by package.ps1).
  sign-linux     Emits Ed25519-signed update-linux-x64.json for AppImage (+ optional .zsync) & .deb.
  sign-manifest  Signs update-win64.json or update-linux-x64.json from already-built artifacts
                 (compatible with scripts/package.ps1 and .github/workflows/release.yml).
  verify         Verifies Ed25519 signature, schema, and path safety of an update manifest envelope.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
from typing import Any

DOMAIN_PREFIX = b"animehub-update-manifest-v1\n"
UPDATER_PROTOCOL_VERSION = 1

SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
RESERVED_WIN_NAMES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    *{f"COM{i}" for i in range(1, 10)},
    *{f"LPT{i}" for i in range(1, 10)},
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> tuple[int, str]:
    h = hashlib.sha256()
    size = 0
    with path.open("rb") as fh:
        while True:
            chunk = fh.read(65536)
            if not chunk:
                break
            size += len(chunk)
            h.update(chunk)
    return size, h.hexdigest()


def default_release_seq_from_version(version: str) -> int:
    match = SEMVER_RE.match(version)
    if not match:
        raise RuntimeError(f"Invalid SemVer: {version}")
    major, minor, patch = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    return max(1, major * 1_000_000 + minor * 1_000 + patch)


def is_safe_relative_path(rel_path: str) -> bool:
    if not rel_path or len(rel_path) > 512 or rel_path.startswith("/") or rel_path.endswith("/"):
        return False
    for ch in rel_path:
        code = ord(ch)
        if code < 0x20 or code == 0x7F or ch in '\\:*?"<>|':
            return False
    segments = rel_path.split("/")
    if not segments or segments[0].lower() == ".update":
        return False
    for seg in segments:
        if not seg or seg in (".", "..") or seg.endswith((" ", ".")):
            return False
        stem = seg.split(".", 1)[0].upper()
        if stem in RESERVED_WIN_NAMES:
            return False
    return True


def compress_zstd(data: bytes, level: int = 19) -> bytes:
    try:
        import zstandard as zstd  # type: ignore[import-not-found]

        compressor = zstd.ZstdCompressor(level=level)
        return compressor.compress(data)
    except ImportError:
        zstd_bin = shutil.which("zstd")
        if not zstd_bin:
            raise RuntimeError(
                "Neither Python 'zstandard' package nor 'zstd' CLI is available."
            )
        proc = subprocess.run(
            [zstd_bin, f"-{level}", "--stdout", "-"],
            input=data,
            capture_output=True,
            check=True,
        )
        return proc.stdout


def load_ed25519_private_key(key_env: str | None, key_file: str | None) -> Any:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

    raw: bytes | None = None
    if key_file:
        raw = Path(key_file).read_bytes()
    else:
        env_name = key_env or "UPDATER_SIGNING_KEY"
        env_val = os.environ.get(env_name, "").strip()
        if not env_val:
            raise RuntimeError(
                f"Provide --key-file or set environment variable {env_name} to sign the manifest."
            )
        raw = env_val.encode("utf-8")

    stripped = raw.strip()
    if stripped.startswith(b"-----BEGIN"):
        loaded = serialization.load_pem_private_key(stripped, password=None)
        if not isinstance(loaded, Ed25519PrivateKey):
            raise RuntimeError("PEM key is not an Ed25519 private key.")
        return loaded

    if len(raw) == 32:
        return Ed25519PrivateKey.from_private_bytes(raw)

    text = stripped.decode("ascii", errors="strict")
    if len(text) == 64 and all(c in "0123456789abcdefABCDEF" for c in text):
        return Ed25519PrivateKey.from_private_bytes(bytes.fromhex(text))

    decoded = base64.b64decode(text, validate=True)
    if len(decoded) == 32:
        return Ed25519PrivateKey.from_private_bytes(decoded)
    if len(decoded) == 64:
        return Ed25519PrivateKey.from_private_bytes(decoded[:32])
    raise RuntimeError(
        "Unsupported Ed25519 private key format (expected PEM, 32-byte raw, hex, or base64)."
    )


def load_ed25519_public_key(pubkey_text: str) -> Any:
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

    trimmed = pubkey_text.strip()
    if trimmed.startswith("-----BEGIN"):
        loaded = serialization.load_pem_public_key(trimmed.encode("utf-8"))
        if not isinstance(loaded, Ed25519PublicKey):
            raise RuntimeError("PEM key is not an Ed25519 public key.")
        return loaded
    if len(trimmed) == 64 and all(c in "0123456789abcdefABCDEF" for c in trimmed):
        return Ed25519PublicKey.from_public_bytes(bytes.fromhex(trimmed))
    decoded = base64.b64decode(trimmed, validate=True)
    if len(decoded) == 32:
        return Ed25519PublicKey.from_public_bytes(decoded)
    raise RuntimeError(
        "Unsupported Ed25519 public key format (expected 64-char hex, base64, or PEM)."
    )


def sign_manifest_payload(
    payload_obj: dict[str, Any], key_id: str, private_key: Any
) -> dict[str, Any]:
    raw_payload = json.dumps(
        payload_obj, ensure_ascii=False, separators=(",", ":"), sort_keys=False
    ).encode("utf-8")
    signature = private_key.sign(DOMAIN_PREFIX + raw_payload)
    return {
        "schemaVersion": 1,
        "keyId": key_id,
        "signature": base64.b64encode(signature).decode("ascii"),
        "payload": base64.b64encode(raw_payload).decode("ascii"),
    }


def classify_win64_group(rel_path: str) -> tuple[int, str]:
    lower = rel_path.lower()
    if lower in ("animehub.exe", "animehubupdater.exe"):
        return (0, "app")
    if lower.startswith("qml/"):
        return (1, "qml")
    if lower.startswith("engines/"):
        return (3, "engines")
    if lower.startswith(("licenses/", "license", "third-party")):
        return (4, "docs")
    return (2, "qt")


def build_deterministic_ustar(
    stage_dir: Path, member_rel_paths: list[str]
) -> tuple[bytes, list[dict[str, Any]]]:
    buf = io.BytesIO()
    members_meta: list[dict[str, Any]] = []
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as tf:
        for rel in sorted(member_rel_paths):
            if not is_safe_relative_path(rel):
                raise RuntimeError(f"Unsafe path in stage: {rel}")
            full = stage_dir / Path(*rel.split("/"))
            data = full.read_bytes()
            info = tarfile.TarInfo(name=rel)
            info.size = len(data)
            info.mtime = 0
            info.mode = 0o644
            info.uid = 0
            info.gid = 0
            info.uname = ""
            info.gname = ""
            tf.addfile(info, io.BytesIO(data))
            members_meta.append(
                {
                    "path": rel,
                    "size": len(data),
                    "sha256": sha256_bytes(data),
                }
            )
    return buf.getvalue(), members_meta


def build_urls(base_url: str, extra_mirrors: list[str], filename: str) -> list[str]:
    urls: list[str] = []
    for base in [base_url, *extra_mirrors]:
        cleaned = base.rstrip("/")
        url = f"{cleaned}/{filename}"
        if url not in urls:
            urls.append(url)
    return urls


def make_artifact_entry(
    file_path: Path, base_url: str, extra_mirrors: list[str], zsync_path: Path | None = None
) -> dict[str, Any]:
    size, digest = sha256_file(file_path)
    entry: dict[str, Any] = {
        "filename": file_path.name,
        "size": size,
        "sha256": digest,
        "urls": build_urls(base_url, extra_mirrors, file_path.name),
    }
    if zsync_path is not None and zsync_path.is_file():
        entry["zsyncUrls"] = build_urls(base_url, extra_mirrors, zsync_path.name)
    return entry


def read_notes(notes_file: str | None) -> str:
    if not notes_file:
        return ""
    return Path(notes_file).read_text(encoding="utf-8")


def pack_win64_files(
    stage_dir: Path, pack_path: Path, version: str, release_seq: int, zstd_level: int
) -> tuple[int, list[dict[str, Any]]]:
    all_files: list[str] = []
    for root, dirs, files in os.walk(stage_dir):
        dirs[:] = sorted(d for d in dirs if d.lower() != ".update")
        for fname in sorted(files):
            full = Path(root) / fname
            if full.is_symlink():
                raise RuntimeError(f"Symlinks are forbidden in stage directory: {full}")
            rel = full.relative_to(stage_dir).as_posix()
            if rel.lower() == "installed-manifest.json":
                continue
            if not is_safe_relative_path(rel):
                raise RuntimeError(f"Unsafe relative path in stage: {rel}")
            all_files.append(rel)

    if not all_files:
        raise RuntimeError(f"Stage directory '{stage_dir}' contains no files.")

    qml_files = sorted(p for p in all_files if p.lower().startswith("qml/"))
    regular_files = [p for p in all_files if not p.lower().startswith("qml/")]
    regular_files.sort(key=lambda p: (classify_win64_group(p)[0], p.lower()))

    manifest_files: list[dict[str, Any]] = []
    installed_files_map: dict[str, dict[str, Any]] = {}
    pack_offset = 0

    pack_path.parent.mkdir(parents=True, exist_ok=True)
    with pack_path.open("wb") as pack_fh:
        app_files = [p for p in regular_files if classify_win64_group(p)[0] == 0]
        other_files = [p for p in regular_files if classify_win64_group(p)[0] > 0]

        def write_regular(rel: str) -> None:
            nonlocal pack_offset
            full = stage_dir / Path(*rel.split("/"))
            raw_bytes = full.read_bytes()
            digest = sha256_bytes(raw_bytes)
            compressed = compress_zstd(raw_bytes, level=zstd_level)
            pack_fh.write(compressed)
            _, group = classify_win64_group(rel)
            manifest_files.append(
                {
                    "path": rel,
                    "kind": "file",
                    "encoding": "zstd",
                    "size": len(raw_bytes),
                    "sha256": digest,
                    "packOffset": pack_offset,
                    "packSize": len(compressed),
                    "group": group,
                    "executable": rel.lower().endswith(".exe"),
                }
            )
            installed_files_map[rel] = {"size": len(raw_bytes), "sha256": digest}
            pack_offset += len(compressed)

        for rel in app_files:
            write_regular(rel)

        if qml_files:
            tar_bytes, members_meta = build_deterministic_ustar(stage_dir, qml_files)
            tar_sha = sha256_bytes(tar_bytes)
            compressed_tar = compress_zstd(tar_bytes, level=zstd_level)
            pack_fh.write(compressed_tar)
            manifest_files.append(
                {
                    "path": "qml",
                    "kind": "solid-tar",
                    "encoding": "tar+zstd",
                    "size": len(tar_bytes),
                    "sha256": tar_sha,
                    "packOffset": pack_offset,
                    "packSize": len(compressed_tar),
                    "group": "qml",
                    "executable": False,
                    "members": members_meta,
                }
            )
            for m in members_meta:
                installed_files_map[m["path"]] = {"size": m["size"], "sha256": m["sha256"]}
            pack_offset += len(compressed_tar)

        for rel in other_files:
            write_regular(rel)

    installed_manifest = {
        "schemaVersion": 1,
        "version": version,
        "releaseSeq": release_seq,
        "files": installed_files_map,
    }
    (stage_dir / "installed-manifest.json").write_text(
        json.dumps(installed_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return pack_offset, manifest_files


def sidecar_index_path(pack_path: Path) -> Path:
    return pack_path.with_name(pack_path.name + ".index.json")


def cmd_pack_win64(args: argparse.Namespace) -> int:
    if not SEMVER_RE.match(args.version):
        raise RuntimeError(f"Invalid SemVer: {args.version}")
    release_seq = (
        args.release_seq
        if args.release_seq is not None
        else default_release_seq_from_version(args.version)
    )
    if release_seq <= 0:
        raise RuntimeError("--release-seq must be a positive integer.")

    stage_arg = args.stage or args.source
    if not stage_arg:
        raise RuntimeError("Provide --stage (or --source).")
    stage_dir = Path(stage_arg).resolve()

    if args.output:
        pack_path = Path(args.output).resolve()
        out_dir = pack_path.parent
    elif args.out_dir:
        out_dir = Path(args.out_dir).resolve()
        pack_path = out_dir / f"AnimeHub-{args.version}-win64.ahpack"
    else:
        raise RuntimeError("Provide --out-dir or --output.")

    pack_size, manifest_files = pack_win64_files(
        stage_dir, pack_path, args.version, release_seq, args.zstd_level
    )

    # Two-step mode (`--output` without `--base-url`): save sidecar index for `sign-manifest`.
    if args.output and not args.base_url:
        sidecar = {
            "schemaVersion": 1,
            "version": args.version,
            "releaseSeq": release_seq,
            "pack": pack_path.name,
            "files": manifest_files,
        }
        sidecar_index_path(pack_path).write_text(
            json.dumps(sidecar, indent=2) + "\n", encoding="utf-8"
        )
        print(f"Packed {pack_path.name} ({pack_size} bytes, {len(manifest_files)} entries)")
        return 0

    if not args.base_url:
        raise RuntimeError("--base-url is required to emit signed update-win64.json.")

    extra_mirrors: list[str] = args.extra_mirror_url or []
    payload: dict[str, Any] = {
        "schemaVersion": 1,
        "platform": "win64",
        "channel": args.channel,
        "version": args.version,
        "releaseSeq": release_seq,
        "publishedAt": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "isMajor": bool(args.is_major),
        "minUpdaterProtocol": UPDATER_PROTOCOL_VERSION,
        "requiresFullInstaller": bool(args.requires_full_installer),
        "bridgeVersion": args.bridge_version or "",
        "notes": read_notes(args.notes_file),
        "releaseUrl": args.release_url or "",
        "revokedKeyIds": list(args.revoked_key_id or []),
        "pack": make_artifact_entry(pack_path, args.base_url, extra_mirrors),
        "files": manifest_files,
    }
    if args.installer:
        payload["installer"] = make_artifact_entry(
            Path(args.installer).resolve(), args.base_url, extra_mirrors
        )
    if args.portable:
        payload["portable"] = make_artifact_entry(
            Path(args.portable).resolve(), args.base_url, extra_mirrors
        )

    private_key = load_ed25519_private_key(args.key_env, args.key_file)
    envelope = sign_manifest_payload(payload, args.key_id, private_key)
    out_manifest = out_dir / "update-win64.json"
    out_manifest.write_text(json.dumps(envelope, indent=2) + "\n", encoding="utf-8")

    print(
        f"Packed {pack_path.name} ({pack_size} bytes, {len(manifest_files)} entries) -> {out_manifest}"
    )
    return 0


def cmd_sign_linux(args: argparse.Namespace) -> int:
    if not SEMVER_RE.match(args.version):
        raise RuntimeError(f"Invalid SemVer: {args.version}")
    release_seq = (
        args.release_seq
        if args.release_seq is not None
        else default_release_seq_from_version(args.version)
    )
    if release_seq <= 0:
        raise RuntimeError("--release-seq must be a positive integer.")

    out_manifest = (
        Path(args.output).resolve()
        if args.output
        else (Path(args.out_dir).resolve() / "update-linux-x64.json")
    )
    out_manifest.parent.mkdir(parents=True, exist_ok=True)
    extra_mirrors: list[str] = args.extra_mirror_url or []

    appimage_path = Path(args.appimage).resolve()
    zsync_path = Path(args.zsync).resolve() if args.zsync else None

    payload: dict[str, Any] = {
        "schemaVersion": 1,
        "platform": "linux-x64",
        "channel": args.channel,
        "version": args.version,
        "releaseSeq": release_seq,
        "publishedAt": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "isMajor": bool(args.is_major),
        "minUpdaterProtocol": UPDATER_PROTOCOL_VERSION,
        "requiresFullInstaller": bool(args.requires_full_installer),
        "bridgeVersion": args.bridge_version or "",
        "notes": read_notes(args.notes_file),
        "releaseUrl": args.release_url or "",
        "revokedKeyIds": list(args.revoked_key_id or []),
        "appimage": make_artifact_entry(appimage_path, args.base_url, extra_mirrors, zsync_path),
        "files": [],
    }
    if args.deb:
        payload["deb"] = make_artifact_entry(
            Path(args.deb).resolve(), args.base_url, extra_mirrors
        )

    private_key = load_ed25519_private_key(args.key_env, args.key_file)
    envelope = sign_manifest_payload(payload, args.key_id, private_key)
    out_manifest.write_text(json.dumps(envelope, indent=2) + "\n", encoding="utf-8")
    print(f"Signed Linux manifest -> {out_manifest}")
    return 0


def cmd_sign_manifest(args: argparse.Namespace) -> int:
    if args.platform == "linux-x64":
        return cmd_sign_linux(args)
    if args.platform != "win64":
        raise RuntimeError(f"Unsupported --platform '{args.platform}'")
    if not SEMVER_RE.match(args.version):
        raise RuntimeError(f"Invalid SemVer: {args.version}")
    if not args.pack:
        raise RuntimeError("--pack is required when signing win64 manifest.")

    pack_path = Path(args.pack).resolve()
    idx_path = sidecar_index_path(pack_path)
    if not idx_path.is_file():
        raise RuntimeError(
            f"Missing pack index sidecar '{idx_path}'. Run pack-win64 first."
        )
    idx_data = json.loads(idx_path.read_text(encoding="utf-8"))
    release_seq = (
        args.release_seq
        if args.release_seq is not None
        else int(idx_data.get("releaseSeq") or default_release_seq_from_version(args.version))
    )

    out_manifest = (
        Path(args.output).resolve()
        if args.output
        else (Path(args.out_dir).resolve() / "update-win64.json")
    )
    out_manifest.parent.mkdir(parents=True, exist_ok=True)
    extra_mirrors: list[str] = args.extra_mirror_url or []

    payload: dict[str, Any] = {
        "schemaVersion": 1,
        "platform": "win64",
        "channel": args.channel,
        "version": args.version,
        "releaseSeq": release_seq,
        "publishedAt": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "isMajor": bool(args.is_major),
        "minUpdaterProtocol": UPDATER_PROTOCOL_VERSION,
        "requiresFullInstaller": bool(args.requires_full_installer),
        "bridgeVersion": args.bridge_version or "",
        "notes": read_notes(args.notes_file),
        "releaseUrl": args.release_url or "",
        "revokedKeyIds": list(args.revoked_key_id or []),
        "pack": make_artifact_entry(pack_path, args.base_url, extra_mirrors),
        "files": idx_data.get("files", []),
    }
    if args.installer:
        payload["installer"] = make_artifact_entry(
            Path(args.installer).resolve(), args.base_url, extra_mirrors
        )
    if args.portable:
        payload["portable"] = make_artifact_entry(
            Path(args.portable).resolve(), args.base_url, extra_mirrors
        )

    private_key = load_ed25519_private_key(args.key_env, args.key_file)
    envelope = sign_manifest_payload(payload, args.key_id, private_key)
    out_manifest.write_text(json.dumps(envelope, indent=2) + "\n", encoding="utf-8")
    # Clean up temporary index sidecar so dist/ upload stays clean.
    idx_path.unlink(missing_ok=True)
    print(f"Signed win64 manifest -> {out_manifest}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    manifest_path = Path(args.manifest).resolve()
    envelope = json.loads(manifest_path.read_text(encoding="utf-8"))
    if envelope.get("schemaVersion") != 1:
        raise RuntimeError("Envelope schemaVersion must be 1.")

    raw_payload = base64.b64decode(envelope["payload"], validate=True)
    sig_text = envelope["signature"].strip()
    if len(sig_text) == 128 and all(c in "0123456789abcdefABCDEF" for c in sig_text):
        sig_bytes = bytes.fromhex(sig_text)
    else:
        sig_bytes = base64.b64decode(sig_text, validate=True)
    if len(sig_bytes) != 64:
        raise RuntimeError("Signature is not 64 bytes.")

    pubkey = load_ed25519_public_key(args.pubkey)
    pubkey.verify(sig_bytes, DOMAIN_PREFIX + raw_payload)

    payload = json.loads(raw_payload.decode("utf-8"))
    if payload.get("schemaVersion") != 1:
        raise RuntimeError("Payload schemaVersion must be 1.")
    if not SEMVER_RE.match(payload.get("version", "")):
        raise RuntimeError("Invalid payload version.")
    if not isinstance(payload.get("releaseSeq"), int) or payload["releaseSeq"] <= 0:
        raise RuntimeError("Invalid payload releaseSeq.")
    if payload.get("platform") not in ("win64", "linux-x64"):
        raise RuntimeError(f"Unsupported platform: {payload.get('platform')}")

    for entry in payload.get("files", []):
        if not is_safe_relative_path(entry.get("path", "")):
            raise RuntimeError(f"Unsafe path in manifest: {entry.get('path')}")
        if entry.get("kind") == "solid-tar":
            for m in entry.get("members", []):
                if not is_safe_relative_path(m.get("path", "")):
                    raise RuntimeError(f"Unsafe solid-tar member path: {m.get('path')}")

    print(
        f"OK: verified {payload['platform']} v{payload['version']} (seq={payload['releaseSeq']}, keyId={envelope.get('keyId')})"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="AnimeHub Desktop update packager and Ed25519 signer"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    def add_common_sign_args(p: argparse.ArgumentParser, require_base_url: bool = True) -> None:
        p.add_argument("--version", required=True)
        p.add_argument("--release-seq", type=int)
        p.add_argument("--out-dir")
        p.add_argument("--output")
        p.add_argument("--base-url", required=require_base_url)
        p.add_argument("--extra-mirror-url", action="append", default=[])
        p.add_argument("--channel", default="stable")
        p.add_argument("--notes-file")
        p.add_argument("--release-url", default="")
        p.add_argument("--key-env")
        p.add_argument("--key-file")
        p.add_argument("--key-id", default="release-2026")
        p.add_argument("--revoked-key-id", action="append", default=[])
        p.add_argument("--is-major", action="store_true")
        p.add_argument("--requires-full-installer", action="store_true")
        p.add_argument("--bridge-version", default="")

    p_win = sub.add_parser("pack-win64", help="Build .ahpack and optionally signed update-win64.json")
    p_win.add_argument("--stage")
    p_win.add_argument("--source")
    p_win.add_argument("--installer")
    p_win.add_argument("--portable")
    p_win.add_argument("--zstd-level", type=int, default=19)
    add_common_sign_args(p_win, require_base_url=False)
    p_win.set_defaults(func=cmd_pack_win64)

    p_lin = sub.add_parser("sign-linux", help="Sign update-linux-x64.json")
    p_lin.add_argument("--appimage", required=True)
    p_lin.add_argument("--zsync")
    p_lin.add_argument("--deb")
    add_common_sign_args(p_lin, require_base_url=True)
    p_lin.set_defaults(func=cmd_sign_linux)

    p_sign = sub.add_parser("sign-manifest", help="Sign win64 or linux-x64 manifest from built assets")
    p_sign.add_argument("--platform", required=True, choices=["win64", "linux-x64"])
    p_sign.add_argument("--pack")
    p_sign.add_argument("--installer")
    p_sign.add_argument("--portable")
    p_sign.add_argument("--appimage")
    p_sign.add_argument("--zsync")
    p_sign.add_argument("--deb")
    add_common_sign_args(p_sign, require_base_url=True)
    p_sign.set_defaults(func=cmd_sign_manifest)

    p_ver = sub.add_parser("verify", help="Verify signed update manifest envelope")
    p_ver.add_argument("--manifest", required=True)
    p_ver.add_argument("--pubkey", required=True)
    p_ver.set_defaults(func=cmd_verify)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
