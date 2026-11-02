#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
"""tools/answered.py against made-up issues and a stand-in GitHub: `unanswered` while a person waits,
`answered` once the developer has written, one note after thirty minutes with no answer, a report an
owner wrote by hand counted as that person's message - and no note when the developer writes first,
twice in one wait or in one day, three hours late, for a wait from before it was switched on, on a
report the problem reporter found or an issue left out; the sweep's fifteen minutes; and every bot
here ending its messages the same way.

    python3 tools/test-answered.py"""
import contextlib
import datetime
import importlib.util
import io
import json
import os
import sys
import tempfile
import urllib.error
import urllib.parse

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import answered  # noqa: E402
import reminder  # noqa: E402
import thanks  # noqa: E402

spec = importlib.util.spec_from_file_location('release_notice', os.path.join(here, 'release-notice.py'))
release_notice = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release_notice)

failures = 0


def check(name, got, want):
    global failures
    ok = got == want
    failures += 0 if ok else 1
    print('%s %s%s' % ('ok  ' if ok else 'FAIL', name, '' if ok else ' - got %r, want %r' % (got, want)))


T0 = datetime.datetime(2026, 11, 20, 12, 0, tzinfo=datetime.timezone.utc)
DEV = {'login': answered.OWNER, 'type': 'User'}
BOT = {'login': 'github-actions[bot]', 'type': 'Bot'}
REPORTER = {'login': 'os-xgs-npu-problem-reports[bot]', 'type': 'Bot'}
WAIT_LEFT = 29 * 60 + 5     # a run that starts a minute after the person wrote sleeps this long


def user(login):
    return {'login': login, 'type': 'User'}


def stamp(t):
    return t.strftime('%Y-%m-%dT%H:%M:%SZ')


def mins(m):
    return T0 + datetime.timedelta(minutes=m)


class Clock:
    def __init__(self, t):
        self.t = t

    def __call__(self):
        return self.t


class GitHub:
    """Issues as number -> (issue, comments). What is written is recorded, and labels and comments are
    applied, so a second look sees what the first one did."""

    def __init__(self, clock, *issues):
        self.clock = clock
        self.issues = {i['number']: (i, c) for i, c in issues}
        self.written = []

    def __call__(self, method, path, body=None):
        rest = path.split('/repos/%s/' % answered.REPO, 1)[1]
        if method == 'GET' and rest.startswith('issues?'):
            page = int(rest.rsplit('page=', 1)[1])
            return [i for i, _ in self.issues.values()][(page - 1) * 100:page * 100]
        issue, comments = self.issues[int(rest.split('/')[1].split('?')[0])]
        if method == 'GET' and '/comments' in rest:
            page = int(rest.rsplit('page=', 1)[1])
            return comments[(page - 1) * 100:page * 100]
        if method == 'GET':
            return issue
        self.written.append((method, rest, body))
        if rest.endswith('/labels'):
            issue['labels'] += [{'name': n} for n in body['labels']]
        elif method == 'DELETE':
            gone = urllib.parse.unquote(rest.rsplit('/', 1)[1])
            issue['labels'] = [x for x in issue['labels'] if x['name'] != gone]
        else:
            comments.append({'user': BOT, 'created_at': stamp(self.clock()), 'body': body['body']})
        return None

    def notes(self):
        return [w for w in self.written if w[1].endswith('/comments')]

    def labels(self, number):
        return sorted(x['name'] for x in self.issues[number][0]['labels'])


def issue(number, author, at, comments=(), labels=(), state='open', pull=False):
    i = {'number': number, 'user': author, 'created_at': stamp(at), 'state': state,
         'labels': [{'name': n} for n in labels]}
    if pull:
        i['pull_request'] = {}
    return i, list(comments)


def comment(author, at, body='...'):
    return {'user': author, 'created_at': stamp(at), 'body': body}


def look(gh, number, during=None, **kw):
    """The run of an event on issue N; `during` happens while it waits, as the developer's comment would."""
    slept, said = [], []

    def sleep(seconds):
        slept.append(seconds)
        if during:
            during()
        gh.clock.t += datetime.timedelta(seconds=seconds)

    answered.look(number, gh, gh.clock, sleep, say=said.append, **kw)
    return slept, said


def sweep(gh):
    answered.sweep(gh, gh.clock, say=lambda s: None)


# A person opens an issue, the run starts a minute later: unanswered at once, the rest of the thirty
# minutes waited out, then one note.
gh = GitHub(Clock(mins(1)), issue(1, user('randgh'), mins(0)))
slept, _ = look(gh, 1)
check('a new issue from a person is unanswered', gh.written[0], ('POST', 'issues/1/labels',
                                                                 {'labels': ['unanswered']}))
check('its run waits out the thirty minutes', slept, [WAIT_LEFT])
check('then writes one note', len(gh.notes()), 1)
body = gh.notes()[0][2]['body']
check('the note greets the person', body.startswith('Hello @randgh, thank you for your message. '), True)
check('the note says the developer is away',
      'The developer is not available at the moment and will reply as soon as possible.' in body, True)
check('the note says it is automated', body.endswith('\n\n' + answered.AUTOMATED + '\n\n' + answered.MARK),
      True)
check('the issue stays unanswered', gh.labels(1), ['unanswered'])

# The developer writes during the thirty minutes: no note, and the issue is answered.
gh = GitHub(Clock(mins(1)), issue(2, user('randgh'), mins(0)))
look(gh, 2, during=lambda: gh.issues[2][1].append(comment(DEV, mins(20))))
check('no note when the developer writes within thirty minutes', gh.notes(), [])
check("the developer's comment makes it answered", gh.labels(2), ['answered'])

# The run of the developer's own comment: answered on, unanswered off, nothing else, no wait.
gh = GitHub(Clock(mins(40)), issue(3, user('randgh'), mins(0), [comment(DEV, mins(39))], ['unanswered']))
slept, _ = look(gh, 3)
check("the developer's comment: answered, unanswered off",
      gh.written, [('POST', 'issues/3/labels', {'labels': ['answered']}),
                   ('DELETE', 'issues/3/labels/unanswered', None)])
check('and no wait', slept, [])

# A report the problem reporter found and sent: nothing - no person has written.
gh = GitHub(Clock(mins(45)), issue(4, REPORTER, mins(0), labels=['problem report', 'sent by hand']))
look(gh, 4)
check('a report the problem reporter found: no label, no note', gh.written, [])

# A report an owner wrote by hand: that person's message, waited on from its opening, greeted by no name.
gh = GitHub(Clock(mins(1)), issue(22, REPORTER, mins(0), labels=['problem report', 'sent by hand', 'manual']))
slept, _ = look(gh, 22)
check('a report written by hand is unanswered', gh.labels(22), ['manual', 'problem report', 'sent by hand',
                                                               'unanswered'])
check('and waited on from its opening', slept, [WAIT_LEFT])
check('its note greets nobody by name',
      gh.notes()[0][2]['body'].startswith('Hello, thank you for your message. The developer is not'), True)

# A person's comment on such a report is waited on from the comment; a bot's comment is no word.
gh = GitHub(Clock(mins(61)), issue(5, REPORTER, mins(0), [comment(REPORTER, mins(10), 'Seen again'),
                                                          comment(user('owner5'), mins(60))]))
slept, _ = look(gh, 5)
check("a person's comment on a report is waited on from the comment", slept, [WAIT_LEFT])
check('and brings the note', len(gh.notes()), 1)

# One note for each wait: the person writes again after it.
gh = GitHub(Clock(mins(50)), issue(6, user('randgh'), mins(0), [comment(BOT, mins(30), 'n ' + answered.MARK),
                                                                comment(user('randgh'), mins(45))]))
look(gh, 6)
check('one note for each wait', gh.notes(), [])

# A new wait the same day after the developer answered: no second note that day.
gh = GitHub(Clock(mins(301)), issue(7, user('randgh'), mins(0), [comment(BOT, mins(30), 'n ' + answered.MARK),
                                                                 comment(DEV, mins(120)),
                                                                 comment(user('randgh'), mins(300))]))
slept, _ = look(gh, 7)
check('never two notes in a day on one issue', (gh.notes(), slept), ([], []))

# The same a day later: the new wait has its note.
gh = GitHub(Clock(mins(1561)), issue(8, user('randgh'), mins(0), [comment(BOT, mins(30), 'n ' + answered.MARK),
                                                                  comment(DEV, mins(120)),
                                                                  comment(user('randgh'), mins(1560))]))
look(gh, 8)
check('a day on, a new wait has its note', len(gh.notes()), 1)

# A wait found four hours on: labelled, and no note that late.
gh = GitHub(Clock(mins(240)), issue(9, user('randgh'), mins(0)))
sweep(gh)
check('never later than three hours: the label only', (gh.labels(9), gh.notes()), (['unanswered'], []))

# A wait that began before this was switched on: labelled, no note.
early = answered.START - datetime.timedelta(hours=1)
gh = GitHub(Clock(answered.START + datetime.timedelta(minutes=30)), issue(10, user('randgh'), early))
sweep(gh)
check('a wait from before START: the label only', (gh.labels(10), gh.notes()), (['unanswered'], []))

# The sweep leaves a note to its wait's own run for fifteen minutes, then writes the one it missed.
gh = GitHub(Clock(mins(40)), issue(11, user('randgh'), mins(0), labels=['unanswered']))
sweep(gh)
check("the sweep leaves a wait to its run for fifteen minutes", gh.written, [])
gh.clock.t = mins(50)
sweep(gh)
check('then writes the note the run missed', len(gh.notes()), 1)

# An issue labelled `no auto reply` is left alone, by the event's run and by the sweep.
gh = GitHub(Clock(mins(45)), issue(12, user('someone'), mins(0), labels=['no auto reply']))
slept, _ = look(gh, 12)
sweep(gh)
check('an issue labelled no auto reply is left alone', (gh.written, slept), ([], []))

# A closed issue and a pull request are left alone.
gh = GitHub(Clock(mins(45)), issue(13, user('someone'), mins(0), state='closed'),
            issue(14, user('coder'), mins(0), pull=True))
look(gh, 13)
look(gh, 14)
sweep(gh)
check('a closed issue and a pull request are left alone', gh.written, [])

# Two people waiting are both greeted, for their messages.
gh = GitHub(Clock(mins(31)), issue(15, user('alice'), mins(0), [comment(user('bob'), mins(5)),
                                                                comment(user('alice'), mins(10))]))
look(gh, 15)
check('two people waiting are both greeted',
      gh.notes()[0][2]['body'].startswith('Hello @alice and @bob, thank you for your messages. '), True)

# A dry run writes nothing and says what it would do.
gh = GitHub(Clock(mins(31)), issue(16, user('alice'), mins(0)))
_, said = look(gh, 16, dry_run=True)
check('a dry run writes nothing', gh.written, [])
check('a dry run says what it would do', (said[0], said[1].startswith('would write on #16:')),
      ('#16: would be labelled unanswered', True))

# Labels already right: the sweep writes nothing.
gh = GitHub(Clock(mins(5)), issue(17, user('alice'), mins(0), [comment(DEV, mins(2))], ['answered']))
sweep(gh)
check('labels already right: nothing written', gh.written, [])

# The last word is read past the first hundred comments.
many = [comment(user('alice'), mins(i)) for i in range(149)] + [comment(DEV, mins(149))]
gh = GitHub(Clock(mins(500)), issue(18, user('alice'), mins(0), many))
sweep(gh)
check('the last word is read past the first hundred comments', gh.labels(18), ['answered'])

# A bot's comment - the thank-you, say - is no answer.
gh = GitHub(Clock(mins(31)), issue(19, user('alice'), mins(0), [comment(BOT, mins(1), 'Thank you, @alice')]))
look(gh, 19)
check("a bot's comment is no answer", (gh.labels(19), len(gh.notes())), (['unanswered'], 1))

# A label another run took off a moment earlier is no failure.
class Taken(GitHub):
    def __call__(self, method, path, body=None):
        if method == 'DELETE':
            raise urllib.error.HTTPError(path, 404, 'Label does not exist', {}, None)
        return GitHub.__call__(self, method, path, body)


gh = Taken(Clock(mins(40)), issue(21, user('randgh'), mins(0), [comment(DEV, mins(39))], ['unanswered']))
look(gh, 21)
check('a label already taken off is no failure', ('answered' in gh.labels(21), gh.notes()), (True, []))

# One issue that cannot be read does not stop the sweep; its number is reported.
class Broken(GitHub):
    def __call__(self, method, path, body=None):
        if method == 'GET' and path.endswith('/issues/23'):
            raise ConnectionResetError('connection reset')
        return GitHub.__call__(self, method, path, body)


gh = Broken(Clock(mins(5)), issue(23, user('alice'), mins(0)), issue(24, user('bob'), mins(0)))
failed = answered.sweep(gh, gh.clock, say=lambda s: None)
check('one unreadable issue does not stop the sweep', (failed, gh.labels(24)), ([23], ['unanswered']))

# No person has written any more - the only comment was deleted: whichever label is there comes off.
gh = GitHub(Clock(mins(45)), issue(25, DEV, mins(0), [comment(DEV, mins(1))], ['unanswered']))
sweep(gh)
check('no person any more: the label comes off', gh.labels(25), [])

# The last note came 23 h 45 min before a new wait began. By the time the new note falls due a day has
# passed, so the run waits for it rather than leaving it to the sweep.
gh = GitHub(Clock(mins(1456)), issue(26, user('randgh'), mins(0), [comment(BOT, mins(30), 'n ' + answered.MARK),
                                                                   comment(DEV, mins(120)),
                                                                   comment(user('randgh'), mins(1455))]))
slept, _ = look(gh, 26)
check('a day is counted to the time the note would be written', (slept, len(gh.notes())), ([WAIT_LEFT], 1))


# The comment that began the wait is deleted during it: the note falls due later, and the run waits again.
def deleted():
    gh.issues[27][1][:] = [c for c in gh.issues[27][1] if c['user']['login'] != 'alice']


gh = GitHub(Clock(mins(1)), issue(27, DEV, mins(0), [comment(user('alice'), mins(0)), comment(user('bob'), mins(20))]))
slept, _ = look(gh, 27, during=deleted)
check('a wait that moved later is waited for again', (slept, len(gh.notes())), ([WAIT_LEFT, 1200], 1))
check('and its note greets who is still waiting', gh.notes()[0][2]['body'].startswith('Hello @bob, '), True)

# A run by hand is a concurrency group of its own, so a dry run never cancels a wait.
with open(os.path.join(os.path.dirname(here), '.github', 'workflows', 'answered.yml'), encoding='utf-8') as f:
    flow = f.read()
check('a run by hand has a concurrency group of its own', "format('by-hand-{0}', github.run_id)" in flow, True)

# The workflow's event as main() reads it: the developer's comment makes the issue answered.
gh = GitHub(Clock(mins(40)), issue(28, user('randgh'), mins(0), [comment(DEV, mins(39))], ['unanswered']))
saved = answered.api, sys.argv, os.environ.get('GITHUB_EVENT_PATH')
with tempfile.TemporaryDirectory() as d:
    path = os.path.join(d, 'event.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump({'action': 'created', 'issue': {'number': 28}, 'comment': {'user': DEV}}, f)
    answered.api, sys.argv, os.environ['GITHUB_EVENT_PATH'] = gh, ['answered.py'], path
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            rc = answered.main()
    finally:
        answered.api, sys.argv = saved[0], saved[1]
        if saved[2] is None:
            del os.environ['GITHUB_EVENT_PATH']
        else:
            os.environ['GITHUB_EVENT_PATH'] = saved[2]
check("main() acts on the event's issue", (rc, gh.labels(28)), (0, ['answered']))

# The developer's own issue, no person on it: nothing.
gh = GitHub(Clock(mins(45)), issue(20, DEV, mins(0), [comment(DEV, mins(1))]))
look(gh, 20)
check("the developer's own issue with no person on it: nothing", gh.written, [])

# Every bot here ends its messages with the same line.
check('every bot ends the same way', {thanks.AUTOMATED, reminder.AUTOMATED, release_notice.AUTOMATED},
      {answered.AUTOMATED})

print('%d failure(s)' % failures)
sys.exit(1 if failures else 0)
