<?php

/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * XGS: Certificate Import (plan 01, package 3.5): a certificate file as an owner has it - DER (.der,
 * .cer, .crt), PKCS#7 (.p7b, .p7c) in DER or PEM, PKCS#12 (.pfx, .p12) with its password, or PEM
 * already - read into what System: Trust takes: PEM, one certificate at a time, and one private key.
 * The page hands each certificate to Trust's own API itself (api/trust/cert/add for the one the key
 * belongs to, or every certificate that is no authority; api/trust/ca/add for the authorities), so
 * Trust checks and stores it as it does any import; nothing here writes the configuration.
 *
 * A PKCS#12 file's password is used in memory by PHP's openssl, and, for a file in the old RC2 or 3DES
 * encryption that OpenSSL 3 reads only with its legacy provider, by the openssl command with
 * -legacy - the password then in that command's environment, never on its command line, and the
 * file in a directory of this run's own, removed after. Nothing is logged of either.
 *
 * Included by controllers/OPNsense/XGS/Api/CertimportController.php, and by tools/test-certimport.py
 * with PHP alone.
 */

/* A DER certificate as PEM. */
function xgs_cert_pem(string $der): string
{
    return "-----BEGIN CERTIFICATE-----\n" . chunk_split(base64_encode($der), 64, "\n") .
        "-----END CERTIFICATE-----\n";
}

/* The PEM blocks of one kind in a text, each ending in a newline. */
function xgs_pem_blocks(string $text, string $kind): array
{
    preg_match_all('/-----BEGIN ' . $kind . '-----.+?-----END ' . $kind . '-----/s', $text, $m);
    return array_map(function ($b) {
        return $b . "\n";
    }, $m[0]);
}

/* What the page shows of a certificate, and its PEM; null for what is no certificate. */
function xgs_cert_info(string $pem, ?string $key): ?array
{
    $x = @openssl_x509_parse($pem);
    if ($x === false) {
        return null;
    }
    $bc = (string)($x['extensions']['basicConstraints'] ?? '');
    $name = function ($dn) {
        if (!is_array($dn)) {
            return '';
        }
        foreach (['CN', 'O', 'OU'] as $k) {
            if (!empty($dn[$k])) {
                return is_array($dn[$k]) ? implode(', ', $dn[$k]) : (string)$dn[$k];
            }
        }
        return '';
    };
    return [
        'pem' => $pem,
        'subject' => $name($x['subject'] ?? null),
        'issuer' => $name($x['issuer'] ?? null),
        'ca' => stripos($bc, 'CA:TRUE') !== false,
        'key' => $key !== null && @openssl_x509_check_private_key($pem, $key),
        'until' => gmdate('Y-m-d', (int)($x['validTo_time_t'] ?? 0)),
    ];
}

/*
 * A PKCS#12 file the PHP openssl could not open, through the openssl command and its legacy provider:
 * its certificates and key as PEM text, or null.
 */
function xgs_p12_legacy(string $data, string $password): ?string
{
    $openssl = is_executable('/usr/bin/openssl') ? '/usr/bin/openssl' :
        (is_executable('/usr/local/bin/openssl') ? '/usr/local/bin/openssl' : null);
    if ($openssl === null) {
        return null;
    }
    $dir = sys_get_temp_dir() . '/xgs-certimport-' . bin2hex(random_bytes(8));
    if (!@mkdir($dir, 0700)) {
        return null;
    }
    $in = $dir . '/in.p12';
    file_put_contents($in, $data);
    chmod($in, 0600);
    $env = ['XGS_P12_PASS' => $password, 'PATH' => '/usr/bin:/bin:/usr/local/bin'];
    $p = proc_open([$openssl, 'pkcs12', '-legacy', '-in', $in, '-nodes', '-passin', 'env:XGS_P12_PASS'],
        [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, $dir, $env);
    $out = null;
    if (is_resource($p)) {
        $text = stream_get_contents($pipes[1]);
        fclose($pipes[1]);
        stream_get_contents($pipes[2]);
        fclose($pipes[2]);
        $out = (proc_close($p) === 0 && strpos((string)$text, '-----BEGIN CERTIFICATE-----') !== false) ? $text : null;
    }
    @unlink($in);
    @rmdir($dir);
    return $out;
}

/*
 * The file's certificates and private key: ['status' => 'ok', 'kind' => der|p7b|pfx|pem, 'certs' => [...],
 * 'key' => PEM or null], the certificate the key belongs to first, then those that are no authority,
 * then the authorities; or ['status' => 'error', 'message' => ...].
 */
function xgs_cert_convert(string $data, string $password = ''): array
{
    if (trim($data) === '') {
        return ['status' => 'error', 'message' => 'the file is empty'];
    }
    $pems = [];
    $key = null;
    $kind = '';
    if (strpos($data, '-----BEGIN ') !== false) {
        $pems = xgs_pem_blocks($data, 'CERTIFICATE');
        $kind = 'pem';
        if (empty($pems) && strpos($data, '-----BEGIN PKCS7-----') !== false) {
            $out = [];
            if (@openssl_pkcs7_read($data, $out)) {
                $pems = $out;
                $kind = 'p7b';
            }
        }
        if (preg_match('/-----BEGIN ((?:RSA |EC |DSA |ENCRYPTED )?PRIVATE KEY)-----.+?-----END \1-----/s', $data, $km)) {
            $k = @openssl_pkey_get_private($km[0], $password === '' ? null : $password);
            if ($k === false) {
                return ['status' => 'error', 'message' => 'the private key could not be read: a wrong password, or none given'];
            }
            openssl_pkey_export($k, $key);
        }
    } else {
        $p12 = [];
        if (@openssl_pkcs12_read($data, $p12, $password)) {
            $kind = 'pfx';
            $pems = array_merge([$p12['cert']], array_values($p12['extracerts'] ?? []));
            $key = $p12['pkey'] ?? null;
        } elseif (substr($data, 0, 1) === "\x30" && strpos(substr($data, 0, 8), "\x02\x01\x03") !== false) {
            /* PKCS#12 by its version, 3: the old encryption, or a wrong password */
            $text = xgs_p12_legacy($data, $password);
            if ($text === null) {
                return ['status' => 'error', 'message' => 'the PKCS#12 file could not be opened: a wrong password, or not a certificate file'];
            }
            $kind = 'pfx';
            $pems = xgs_pem_blocks($text, 'CERTIFICATE');
            if (preg_match('/-----BEGIN (?:RSA |EC )?PRIVATE KEY-----.+?-----END (?:RSA |EC )?PRIVATE KEY-----/s', $text, $km)) {
                $key = $km[0] . "\n";
            }
        } else {
            $out = [];
            $wrapped = "-----BEGIN PKCS7-----\n" . chunk_split(base64_encode($data), 64, "\n") . "-----END PKCS7-----\n";
            if (@openssl_pkcs7_read($wrapped, $out)) {
                $kind = 'p7b';
                $pems = $out;
            } elseif (@openssl_x509_read(xgs_cert_pem($data)) !== false) {
                $kind = 'der';
                $pems = [xgs_cert_pem($data)];
            }
        }
    }
    $certs = [];
    foreach ($pems as $pem) {
        $info = xgs_cert_info($pem, $key);
        if ($info !== null) {
            $certs[] = $info;
        }
    }
    if (empty($certs)) {
        return ['status' => 'error', 'message' => 'no certificate was found in the file'];
    }
    usort($certs, function ($a, $b) {
        $rank = function ($c) {
            return $c['key'] ? 0 : ($c['ca'] ? 2 : 1);
        };
        return $rank($a) <=> $rank($b);
    });
    return ['status' => 'ok', 'kind' => $kind, 'certs' => $certs, 'key' => $key];
}
