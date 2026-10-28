"""Validate and render the explicit roadmap; evidence is scoped, never inferred."""
from pathlib import Path
from html import escape
import argparse
import datetime
import json
import re

ROOT = Path(__file__).resolve().parents[1]
STATES = ('completed', 'in_progress', 'partial', 'pending', 'blocked', 'deferred')
COLORS = {'completed': '#c6efce', 'in_progress': '#cfe6ff', 'partial': '#ffe7ad',
          'pending': '#e7e7eb', 'blocked': '#ffd2d2', 'deferred': '#e4d9f5'}

def load(path=None, check_paths=False):
    path = Path(path) if path else ROOT / 'roadmap.json'
    data = json.loads(path.read_text(encoding='utf-8'))
    if data.get('schema_version') != 1:
        raise ValueError('Unsupported roadmap schema')
    if set(data['states']) != set(STATES):
        raise ValueError('Every roadmap state must have a definition')
    datetime.date.fromisoformat(data['updated_on'])
    items = data['items']
    ids = {item['id'] for item in items}
    if len(ids) != len(items):
        raise ValueError('Duplicate roadmap IDs')
    by_id = {item['id']: item for item in items}
    for item in items:
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9-]*', item['id']):
            raise ValueError('Invalid roadmap ID')
        if item['status'] not in STATES:
            raise ValueError('Unknown status: ' + item['id'])
        for key in ('title', 'scope', 'acceptance', 'next_test'):
            if not item.get(key):
                raise ValueError('Missing ' + key + ': ' + item['id'])
        if item.get('parent') and item['parent'] not in ids:
            raise ValueError('Unknown parent: ' + item['id'])
        if any(dep not in ids for dep in item['prerequisites']):
            raise ValueError('Unknown prerequisite: ' + item['id'])
        if item['status'] == 'completed' and (not item['evidence'] or not item['last_verified_on']):
            raise ValueError('Completed scope needs dated evidence: ' + item['id'])
        if item['status'] == 'blocked' and not item.get('blocker'):
            raise ValueError('Blocked scope needs an actual blocker: ' + item['id'])
        if item['last_verified_on']:
            datetime.date.fromisoformat(item['last_verified_on'])
        for record in item['evidence']:
            datetime.date.fromisoformat(record['checked_on'])
            evidence = (ROOT / record['path']).resolve()
            if not evidence.is_relative_to(ROOT) or (check_paths and not evidence.is_file()):
                raise ValueError('Invalid evidence path: ' + record['path'])
        if item['status'] == 'completed' and any(
                child['status'] != 'completed' for child in items if child.get('parent') == item['id']):
            raise ValueError('Completed parent has unfinished subgates: ' + item['id'])
    def visit(key, chain):
        if key in chain:
            raise ValueError('Roadmap dependency/parent cycle: ' + key)
        row = by_id[key]
        for dep in row['prerequisites'] + ([row['parent']] if row.get('parent') else []):
            visit(dep, chain + [key])
    for key in ids:
        visit(key, [])
    return data

def badge(state):
    label = state.replace('_', ' ')
    return ('<span class="roadmap-status roadmap-status-' + state + '" style="display:inline-block;'
            'padding:.18em .6em;border-radius:.4em;border:1px solid #68737d;'
            'font-size:.86em;font-weight:700;color:#14232c;background:' + COLORS[state] + '">' + label + '</span>')

def markdown(data=None):
    data=data or load(check_paths=True)
    lines=['# Engine roadmap status — '+data['updated_on'],'',
           'Generated from `roadmap.json`. States describe only the named scope; no overall completion percentage is implied.','',
           '| Gate | Status | Verified scope / remaining work |','| --- | --- | --- |']
    for row in data['items']:
        lines.append('| '+row['id']+' · '+row['title']+' | '+row['status'].replace('_',' ')+' | '+row['scope'].replace('|','/')+' |')
    lines.extend(['','Build and test instructions: [IMPLEMENTATION.md](IMPLEMENTATION.md). Evidence and next tests are recorded per gate in [roadmap.json](roadmap.json) and shown in the documentation page.',''])
    return '\n'.join(lines)

def render(path=None):
    data = load(path)
    parents = [row for row in data['items'] if not row.get('parent')]
    counts = {state: sum(row['status'] == state for row in parents) for state in STATES}
    summary = ' · '.join(str(counts[state]) + ' ' + state.replace('_', ' ') for state in STATES)
    out = ['<div class="roadmap-live-status"><p><strong>Roadmap status · ' + escape(data['updated_on']) +
           '</strong></p><p>' + escape(summary) + ' across ' + str(len(parents)) +
           ' top-level gates. Subgates are listed separately and are not counted twice. '
           'Counts describe scope states, not percentage completion or a delivery estimate.</p>',
           '<p><a href="roadmap-status.json">Machine-readable roadmap</a>. '
           'Completed means only the stated tested scope; engine-wide RT/PT, DLSS and UE visual parity remain open.</p>',
           '<details class="roadmap-legend"><summary>Status definitions</summary><dl>']
    for state in STATES:
        out.append('<dt>' + badge(state) + '</dt><dd>' + escape(data['states'][state]) + '</dd>')
    out.append('</dl></details><div class="table-wrap"><table><thead><tr><th>Gate / status</th>'
               '<th>Scope and acceptance</th><th>Evidence / last verified</th><th>Prerequisites / next test</th>'
               '</tr></thead><tbody>')
    for row in data['items']:
        evidence = '<br>'.join(escape(record['summary']) + ' (' + escape(record['path']) +
                              ', ' + escape(record['checked_on']) + ')' for record in row['evidence'])
        verified = escape(row['last_verified_on'] or 'No passing verification recorded')
        prefix = ('Subgate of ' + row['parent'] + ' · ') if row.get('parent') else ''
        deps = ', '.join(row['prerequisites']) or 'None'
        out.append('<tr id="roadmap-' + escape(row['id'], quote=True) + '"><td><strong>' +
                   escape(prefix + row['id'] + ' · ' + row['title']) + '</strong><br>' + badge(row['status']) +
                   '</td><td>' + escape(row['scope']) + '<p><strong>Acceptance:</strong> ' + escape(row['acceptance']) +
                   '</p></td><td>' + (evidence or 'No passing evidence recorded.') + '<p><strong>Last verified:</strong> ' +
                   verified + '</p></td><td><strong>Requires:</strong> ' + escape(deps) +
                   '<p><strong>Next test:</strong> ' + escape(row['next_test']) + '</p>' +
                   ('<p><strong>Blocker:</strong> ' + escape(row['blocker']) + '</p>' if row.get('blocker') else '') + '</td></tr>')
    out.append('</tbody></table></div></div>')
    return ''.join(out)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        data = load(check_paths=True)
        print(json.dumps({'status': 'passed', 'items': len(data['items']),
                          'states': {state: sum(row['status'] == state for row in data['items']) for state in STATES}}))
    else:
        print(render())
