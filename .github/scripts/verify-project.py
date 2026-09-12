"""Read-only project docs/evidence checks; --archives checks local historical ZIPs."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / 'docs'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(base, relative):
    rel = PurePosixPath(relative)
    require(not rel.is_absolute() and '..' not in rel.parts, f'Unsafe path: {relative}')
    target = base.joinpath(*rel.parts).resolve()
    require(target.is_relative_to(base.resolve()), f'Escaping path: {relative}')
    return target


def verify_docs():
    files = [ROOT / 'README.md', ROOT / 'AGENTS.md', *DOCS.rglob('*.md')]
    index = (DOCS / 'README.md').read_text(encoding='utf-8')
    for path in files:
        text = path.read_text(encoding='utf-8')
        require(text.strip(), f'Empty document: {path}')
        if path.parent == DOCS and path.name != 'README.md':
            require(f']({path.name})' in index, f'Unindexed document: {path.name}')
        for link in re.findall(r'\]\(([^)]+)\)', text):
            if re.match(r'[a-zA-Z][a-zA-Z0-9+.-]*:', link) or link.startswith('#'):
                continue
            target = (path.parent / link.split('#', 1)[0]).resolve()
            # Historical archives and evidence are deliberately optional in a clone.
            if target.is_relative_to(ROOT / '_dist') or target.is_relative_to(DOCS / 'evidence'):
                continue
            require(target.exists(), f'Broken link in {path.relative_to(ROOT)}: {link}')
    print(f'PASS: {len(files)} documentation files and local links')


def verify_evidence():
    if not (DOCS / 'evidence').exists():
        print('SKIP: optional local docs/evidence directory is absent')
        return
    manifest = json.loads((DOCS / 'evidence/preservation.json').read_text())
    for item in manifest['files']:
        path = safe_path(DOCS, item['path'])
        data = path.read_bytes()
        require(len(data) == item['bytes'] and digest(data) == item['sha256'],
                f'Changed preserved evidence: {item["path"]}')
    print(f'PASS: {len(manifest["files"])} preserved evidence files')


def pfs0_names(data):
    require(data[:4] == b'PFS0', 'Missing PFS0 header')
    count, strings_size = struct.unpack_from('<II', data, 4)
    strings_start = 16 + count * 24
    data_start = strings_start + strings_size
    require(data_start <= len(data), 'PFS0 table exceeds file')
    names = []
    for i in range(count):
        offset, size, name_offset, _ = struct.unpack_from('<QQII', data, 16 + i * 24)
        start = strings_start + name_offset
        require(strings_start <= start < data_start, 'Invalid PFS0 string offset')
        end = data.index(b'\0', start, data_start)
        require(data_start + offset + size <= len(data), 'PFS0 payload exceeds file')
        names.append(data[start:end].decode())
    return sorted(names)


def verify_archives():
    manifest = json.loads((DOCS / 'evidence/ARTIFACT_MANIFEST.json').read_text())
    count = 0
    for release in manifest['releases']:
        if not release['archive']:
            continue
        old = PurePosixPath(release['archive'])
        require(old.parts[0] == 'releases', 'Unexpected historical archive path')
        path = safe_path(ROOT / '_dist', PurePosixPath(*old.parts[1:]).as_posix())
        require(digest(path.read_bytes()) == release['sha256'], f'ZIP hash mismatch: {path.name}')
        with zipfile.ZipFile(path) as archive:
            require(archive.testzip() is None, f'ZIP CRC error: {path.name}')
            names = [entry.filename for entry in archive.infolist() if not entry.is_dir()]
            require(len(names) == len(set(names)) and set(names) == set(release['payload_sha256']),
                    f'Unexpected ZIP payloads: {path.name}')
            for name in names:
                data = archive.read(name)
                require(digest(data) == release['payload_sha256'][name], f'Payload mismatch: {name}')
                require(bool(data) or name.endswith('/boot2.flag'), f'Empty payload: {name}')
            require(pfs0_names(archive.read('atmosphere/contents/4200000000000000/exefs.nsp'))
                    == ['main', 'main.npdm'], 'Unexpected ExeFS files')
            overlay = archive.read('switch/.overlays/sys-tune-overlay.ovl')
            require(release['version'].encode() in overlay, 'Overlay version missing')
            if release['embedded_identity_checks']['overlay_has_commit']:
                require(release['short_commit'].encode() in overlay, 'Overlay commit missing')
        count += 1
    require(count == manifest['original_install_zip_count'], 'Archive count mismatch')
    print(f'PASS: {count} historical ZIPs, payloads, identities and ExeFS structure')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archives', action='store_true', help='Require and verify local _dist archives')
    args = parser.parse_args()
    verify_docs()
    verify_evidence()
    if args.archives:
        verify_archives()
    print('No build, console tests, network operations or Git mutations performed.')


if __name__ == '__main__':
    main()
