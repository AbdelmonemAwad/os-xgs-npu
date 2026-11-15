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

namespace OPNsense\Auth;

/**
 * TACACS+ (plan 01, package 3.10): a server type of System: Access: Servers, beside LDAP and RADIUS, for
 * the web GUI's login and every other service that asks OPNsense's authentication. OPNsense loads every
 * connector in this folder (AuthenticationFactory), and the servers page draws the fields below.
 *
 * The question is asked by FreeBSD's own TACACS+ library, libtacplus, through a small tool built at
 * install with the appliance's cc (tools/tacacs/xgs-tacauth.c, /usr/local/sbin/xgs-tacauth): OPNsense's
 * PHP has no binding to the library. The tool reads the server, the secret, the user and the password
 * on its standard input, so none of them is on a command line.
 *
 * Only the answer is taken from the server: the user must exist on the firewall, with its groups and
 * privileges, as for a RADIUS server whose groups are not synchronised.
 */
class TacacsPlus extends Base implements IAuthConnector
{
    /** the tool that asks the server; a static, so that tools/test-tacacs.py can give it a stand-in */
    private static $tool = '/usr/local/sbin/xgs-tacauth';

    private $host = null;
    private $port = 49;
    private $secret = '';
    private $timeout = 5;
    private $method = 'pap';
    private $lastAuthProperties = [];

    /**
     * type name in configuration
     * @return string
     */
    public static function getType()
    {
        return 'tacplus';
    }

    /**
     * user friendly description of this authenticator
     * @return string
     */
    public function getDescription()
    {
        return gettext('TACACS+');
    }

    /**
     * set connector properties
     * @param array $config connection properties
     */
    public function setProperties($config)
    {
        $this->host = trim((string)($config['host'] ?? ''));
        $this->port = (int)($config['tacplus_port'] ?? 49) ?: 49;
        $this->secret = (string)($config['tacplus_secret'] ?? '');
        $this->timeout = (int)($config['tacplus_timeout'] ?? 5) ?: 5;
        $method = strtolower((string)($config['tacplus_method'] ?? 'pap'));
        $this->method = in_array($method, ['pap', 'ascii', 'chap']) ? $method : 'pap';
    }

    /**
     * the fields of this type on System: Access: Servers
     * @return array
     */
    public function getConfigurationOptions()
    {
        $options = [];
        $options['host'] = [
            'name' => gettext('TACACS+ server'),
            'type' => 'text',
            'default' => '',
            'help' => gettext('The IPv4 address or host name of the server: the TACACS+ library of FreeBSD reaches a server by IPv4 only.'),
            'validate' => function ($value) {
                $value = trim((string)$value);
                if (filter_var($value, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4) !== false ||
                    preg_match('/^(?=.{1,253}$)[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*$/', $value)) {
                    return [];
                }
                return [gettext('The TACACS+ server must be an IPv4 address or a host name.')];
            }
        ];
        $options['tacplus_port'] = [
            'name' => gettext('TACACS+ port'),
            'type' => 'text',
            'default' => '49',
            'help' => gettext('The TCP port of the server; 49 is the one TACACS+ uses.'),
            'validate' => function ($value) {
                return ctype_digit((string)$value) && (int)$value >= 1 && (int)$value <= 65535 ? [] :
                    [gettext('The TACACS+ port must be a number from 1 to 65535.')];
            }
        ];
        $options['tacplus_secret'] = [
            'name' => gettext('TACACS+ shared secret'),
            'type' => 'text',
            'default' => '',
            'help' => gettext('The key the server shares with this firewall, which encrypts what passes between them. Shown on this page as it is typed, and kept in the configuration as the RADIUS secret is. Empty: nothing encrypted, for a lab only.'),
            'validate' => function ($value) {
                return strpbrk((string)$value, "\r\n") === false ? [] :
                    [gettext('The TACACS+ shared secret must be on one line.')];
            }
        ];
        $options['tacplus_timeout'] = [
            'name' => gettext('TACACS+ timeout (s)'),
            'type' => 'text',
            'default' => '5',
            'help' => gettext('How long to wait for the server, in seconds.'),
            'validate' => function ($value) {
                return ctype_digit((string)$value) && (int)$value >= 1 && (int)$value <= 60 ? [] :
                    [gettext('The TACACS+ timeout must be a number of seconds from 1 to 60.')];
            }
        ];
        $options['tacplus_method'] = [
            'name' => gettext('TACACS+ method'),
            'type' => 'dropdown',
            'default' => 'pap',
            'options' => ['pap' => 'PAP', 'ascii' => 'ASCII', 'chap' => 'CHAP'],
            'help' => gettext('How the password is given: PAP sends it, ASCII answers the prompts of the server for the user and the password as a terminal login does, and CHAP sends a response computed from it. Each is encrypted by the shared secret.'),
            'validate' => function ($value) {
                return in_array((string)$value, ['pap', 'ascii', 'chap']) ? [] :
                    [gettext('The TACACS+ method must be PAP, ASCII or CHAP.')];
            }
        ];
        return $options;
    }

    /**
     * return session info
     * @return array mixed named list of authentication properties
     */
    public function getLastAuthProperties()
    {
        return $this->lastAuthProperties;
    }

    /**
     * the server's answer for a user and a password, through xgs-tacauth
     * @param string $username username to authenticate
     * @param string $password user password
     * @return bool authentication status
     */
    protected function _authenticate($username, $password)
    {
        $this->lastAuthProperties = [];
        $this->lastAuthErrors = [];
        $lines = [$this->host, (string)$this->port, $this->secret, (string)$this->timeout, $this->method,
            (string)($_SERVER['REMOTE_ADDR'] ?? ''), (string)$username, (string)$password];
        /* the tool reads one item a line: a line break in any of them - a secret edited into the
         * configuration by hand among them - would move every item after it */
        if (strpbrk(implode('', $lines), "\r\n") !== false || empty($this->host)) {
            return false;
        }
        if (!is_executable(self::$tool)) {
            $this->lastAuthErrors['error'] = 'TACACS+: ' . self::$tool . ' is not installed';
            syslog(LOG_ERR, 'TACACS+: ' . self::$tool . ' is not installed; reinstalling os-xgs-npu builds it');
            return false;
        }
        $proc = proc_open([self::$tool], [0 => ['pipe', 'r'], 1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes);
        if (!is_resource($proc)) {
            $this->lastAuthErrors['error'] = 'TACACS+: ' . self::$tool . ' did not start';
            return false;
        }
        fwrite($pipes[0], implode("\n", $lines) . "\n");
        fclose($pipes[0]);
        $out = trim((string)stream_get_contents($pipes[1]));
        fclose($pipes[1]);
        stream_get_contents($pipes[2]);
        fclose($pipes[2]);
        $rc = proc_close($proc);
        $verdict = strtok($out, ' ');
        $message = trim(substr($out, strlen((string)$verdict)));
        if ($rc === 0 && $verdict === 'PASS') {
            $this->lastAuthProperties['tacacs_message'] = $message;
            return true;
        }
        $this->lastAuthErrors['error'] = 'TACACS+ ' . ($verdict === 'FAIL' ? 'refused' : 'error') . ': ' . $message;
        if ($verdict !== 'FAIL') {
            syslog(LOG_WARNING, sprintf('TACACS+ server %s:%d: %s', $this->host, $this->port, $message));
        }
        return false;
    }
}
