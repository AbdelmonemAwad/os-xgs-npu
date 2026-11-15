<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
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
 * THIS SOFTWARE IS PROVIDED ``AS IS' AND ANY EXPRESS OR IMPLIED WARRANTIES,
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

namespace OPNsense\XGS\Api;

use OPNsense\Base\ApiControllerBase;

/**
 * XGS: Certificate Import (plan 01, package 3.5): a certificate file read into PEM for System: Trust -
 * DER, PKCS#7 or PKCS#12 with its password (scripts/xgs/certimport.inc). It stores nothing: the page
 * hands what this returns to Trust's own API, which checks and stores it as any import.
 */
class CertimportController extends ApiControllerBase
{
    public function convertAction()
    {
        if (!$this->request->isPost()) {
            return ['status' => 'error', 'message' => gettext('this endpoint is POST only')];
        }
        $data = base64_decode((string)$this->request->getPost('data'), true);
        if ($data === false || strlen($data) > 1048576) {
            return ['status' => 'error', 'message' => gettext('The file could not be read, or is over 1 MB.')];
        }
        require_once('/usr/local/opnsense/scripts/xgs/certimport.inc');
        $r = xgs_cert_convert($data, (string)$this->request->getPost('password'));
        /* the library's reasons, each in the page's language */
        $texts = [
            'the file is empty' => gettext('The file is empty.'),
            'the private key could not be read: a wrong password, or none given' =>
                gettext('The private key could not be read: a wrong password, or none given.'),
            'the PKCS#12 file could not be opened: a wrong password, or not a certificate file' =>
                gettext('The PKCS#12 file could not be opened: a wrong password, or not a certificate file.'),
            'no certificate was found in the file' => gettext('No certificate was found in the file.'),
        ];
        if ($r['status'] !== 'ok') {
            $r['message'] = $texts[$r['message']] ?? $r['message'];
        }
        return $r;
    }
}
