# Weasel hang diagnostics

Turn on the opt-in file trace before reproducing a hang. Open PowerShell
under the affected Windows account (not another administrator account).

    New-Item -Path 'HKCU:\Software\Rime\Weasel' -Force | Out-Null
    New-ItemProperty -Path 'HKCU:\Software\Rime\Weasel' -Name DiagnosticTrace -PropertyType DWord -Value 1 -Force | Out-Null

The setting is read once in each process. Restart WeaselServer and the host
applications that load the TSF; signing out and back in reloads older hosts.

Logs are stored by process ID at:

    %TEMP%\rime.weasel\diagnostics\weasel-diagnostic-<PID>.log

Each event records a timestamp, process and thread IDs, stage, event, and
small numeric result or elapsed milliseconds. Typed text, keystrokes,
candidate contents, dictionary data and window titles are never recorded.
Repeated session recovery traces are rate limited to avoid I/O per key.

After reproducing once, collect the most recent traces:

    $dir = Join-Path $env:TEMP 'rime.weasel\diagnostics'
    Get-ChildItem $dir -Filter 'weasel-diagnostic-*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 12 Name,LastWriteTime,Length
    Get-ChildItem $dir -Filter 'weasel-diagnostic-*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 12 | ForEach-Object { "### $($_.Name)"; Get-Content $_.FullName -Tail 100 }

Look for a 'begin' event without its matching 'end' event in the same
process/thread. That is a suspect blocking point, not definitive proof.
Compare the server lifecycle Ready marker against TSF connect, Rime
create-session and client start-session no-ack markers.

Disable tracing after collecting logs:

    Remove-ItemProperty -Path 'HKCU:\Software\Rime\Weasel' -Name DiagnosticTrace -ErrorAction SilentlyContinue

Restart previously traced processes to pick up the disabled setting.
Delete old diagnostic files manually after collecting them.
