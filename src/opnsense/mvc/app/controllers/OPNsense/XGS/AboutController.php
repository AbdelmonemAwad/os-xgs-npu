<?php

/*
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

namespace OPNsense\XGS;

/**
 * XGS: About - the project, its author, its links and addresses, the disclaimer and the licences,
 * in tabs. Static text, and two things read from this firewall: the release installed, from the
 * record install.sh and the Updates page keep (/usr/local/share/os-xgs-npu/installed.json), and every
 * release's changes, from the CHANGELOG.md install.sh puts beside it.
 */
class AboutController extends \OPNsense\Base\IndexController
{
    public function indexAction()
    {
        $installed = [];
        $raw = @file_get_contents('/usr/local/share/os-xgs-npu/installed.json');
        if ($raw !== false) {
            $installed = json_decode($raw, true) ?: [];
        }
        $this->view->release = (string)($installed['release'] ?? '');
        $this->view->commit = substr((string)($installed['commit'] ?? ''), 0, 12);
        $this->view->installedDate = (string)($installed['date'] ?? '');
        $this->view->installedVersion = ltrim((string)($installed['release'] ?? ''), 'v');
        $this->view->releases = $this->releases();
        $this->view->pick('OPNsense/XGS/about');
    }

    /**
     * CHANGELOG.md as releases: each "## <version> - <date>" with its summary and its sections of
     * items, an item's pull request kept apart as its number and address. Markdown is taken out, not
     * rendered: the page shows text, and the only links are the pull requests on GitHub.
     */
    private function releases()
    {
        $raw = @file_get_contents('/usr/local/share/os-xgs-npu/CHANGELOG.md');
        if ($raw === false) {
            return [];
        }
        $out = [];
        $at = -1;
        $section = null;
        $para = '';
        $flush = function () use (&$out, &$at, &$para) {
            /* a paragraph that was only a photograph leaves nothing, and is not shown */
            if ($at >= 0 && $para !== '' && trim($this->plain($para)) !== '') {
                $out[$at]['summary'][] = trim($this->plain($para));
            }
            $para = '';
        };
        foreach (preg_split('/\r?\n/', $raw) as $line) {
            if (preg_match('/^## (\d[0-9A-Za-z.+-]*) - (\d{4}-\d{2}-\d{2})\s*$/', $line, $m)) {
                $flush();
                $out[] = ['version' => $m[1], 'date' => $m[2], 'summary' => [], 'sections' => []];
                $at = count($out) - 1;
                $section = null;
                continue;
            }
            if ($at < 0) {
                continue;
            }
            if (preg_match('/^### (.+)$/', $line, $m)) {
                $flush();
                $section = count($out[$at]['sections']);
                $out[$at]['sections'][] = ['name' => trim($m[1]), 'items' => []];
                continue;
            }
            if (preg_match('/^- (.+)$/', $line, $m) && $section !== null) {
                $out[$at]['sections'][$section]['items'][] = $this->item($m[1]);
                continue;
            }
            if (trim($line) === '') {
                $flush();
                continue;
            }
            if ($section === null) {
                $para .= ($para === '' ? '' : ' ') . trim($line);
            } elseif (!empty($out[$at]['sections'][$section]['items'])) {
                /* a list item's next line */
                $n = count($out[$at]['sections'][$section]['items']) - 1;
                $out[$at]['sections'][$section]['items'][$n]['text'] .= ' ' . $this->plain(trim($line));
            }
        }
        $flush();
        return $out;
    }

    private function item($text)
    {
        $pr = '';
        $url = '';
        if (preg_match('/^(.*?)\s*\(\[#(\d+)\]\((https:\/\/github\.com\/AbdelmonemAwad\/[A-Za-z0-9._\/-]+)\)\)\s*$/', $text, $m)) {
            $text = $m[1];
            $pr = $m[2];
            $url = $m[3];
        }
        return ['text' => $this->plain($text), 'pr' => $pr, 'url' => $url];
    }

    private function plain($text)
    {
        /* A photograph the release's page shows ("![what](address)") is left out here: the page
           shows text, and until 2026-10-26 a release's photographs read "!XGS: Overview, ..." */
        $text = preg_replace('/!\[[^\]]*\]\([^)]*\)/', '', $text);
        $text = preg_replace('/\[([^\]]*)\]\([^)]*\)/', '$1', $text);
        return str_replace(['**', '`', '~~'], '', $text);
    }
}
