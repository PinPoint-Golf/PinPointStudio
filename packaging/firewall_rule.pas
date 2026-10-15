{ ─────────────────────────────────────────────────────────────────────────────
  Windows Firewall: one inbound allow rule for PinPointStudio.exe, on every profile.

  PinPoint LISTENS — the phone dials the PPCP host, and a GSPro Open Connect bridge
  dials port 921 — so the first bind of an unregistered exe raises the "Security
  Alert" box. Cancel writes a permanent BLOCK rule; Allow with the default box
  writes allow rules for Private only, and a studio PC is often classified Public.
  Either way Windows never asks again, block beats allow, and pairing just fails.
  See docs/implementation/windows_release_runbook.md, ONCE §4.

  The installer is per-user (PrivilegesRequired=lowest, so WinSparkle can update in
  place) and a standard user cannot write a firewall rule. So:
    1. ssPostInstall reads the rules unelevated (allowed) and stops if the exe
       already has an enabled inbound allow rule on profile Any and no enabled
       inbound block rule. Every upgrade after the first takes this path: no UAC.
    2. Otherwise it elevates ONCE (ShellExec 'runas'), deletes every inbound rule
       that names the exe or carries our group — the alert's block/Private rules,
       and our own rule for an old install path — and adds the one rule.
       Program-scoped, not port-scoped: narrower, and it survives a port change.
  A declined UAC prompt is not an install failure: the app installs, the next
  run of the installer asks again, and the user is told why.

  The rule is left behind on uninstall. Removing it needs the same elevation, and
  an allow rule for a deleted exe admits nothing.

  Included by CPack via CPACK_INNOSETUP_CODE_FILES (cmake/PinPointPackaging.cmake);
  this file is already inside [Code]. The -cuda package clears that list.
  ───────────────────────────────────────────────────────────────────────────── }

const
  PpFwRuleName = 'PinPoint Studio';

function PpFwQuote(const S: String): String;
var
  T: String;
begin
  T := S;
  StringChangeEx(T, '''', '''''', True);   { PowerShell single-quoted literal }
  Result := '''' + T + '''';
end;

function PpFwPowerShell(): String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

function PpFwArgs(const Body: String): String;
begin
  { No double quotes inside Body: it travels as one quoted -Command argument. }
  Result := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -Command "' + Body + '"';
end;

{ The exe's enabled inbound rules. Paths compare case-insensitively (-eq), which
  matters: the alert stores the path lowercased. }
function PpFwExeRules(const Exe: String): String;
begin
  Result :=
    '$p = ' + PpFwQuote(Exe) + '; ' +
    '$r = @(Get-NetFirewallApplicationFilter | Where-Object { $_.Program -eq $p } | Get-NetFirewallRule | ' +
    'Where-Object { $_.Direction -eq ''Inbound'' }); ';
end;

{ True when no elevation is needed. }
function PpFwRuleIsGood(const Exe: String): Boolean;
var
  Body: String;
  Code: Integer;
begin
  Body := PpFwExeRules(Exe) +
    '$on = @($r | Where-Object { $_.Enabled -eq ''True'' }); ' +
    'if (@($on | Where-Object { $_.Action -eq ''Block'' }).Count) { exit 1 }; ' +
    'if (@($on | Where-Object { $_.Action -eq ''Allow'' -and $_.Profile -eq ''Any'' }).Count) { exit 0 }; ' +
    'exit 1';
  if not Exec(PpFwPowerShell(), PpFwArgs(Body), '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
    Log('Firewall: could not run the rule check (' + SysErrorMessage(Code) + ')');
    Result := False;
  end else
    Result := (Code = 0);
end;

{ Runs elevated. Removal is best-effort (a rule can be in both sets); the add is
  not, and the caller re-runs the check rather than trusting this exit code. }
function PpFwFixBody(const Exe: String): String;
begin
  Result := PpFwExeRules(Exe) +
    '$r += @(Get-NetFirewallRule -Group ' + PpFwQuote(PpFwRuleName) + ' -ErrorAction SilentlyContinue); ' +
    '$r | Remove-NetFirewallRule -ErrorAction SilentlyContinue; ' +
    'try { New-NetFirewallRule -DisplayName ' + PpFwQuote(PpFwRuleName) +
    ' -Group ' + PpFwQuote(PpFwRuleName) +
    ' -Description ''Lets a paired phone and a launch monitor bridge connect to PinPoint Studio.''' +
    ' -Direction Inbound -Action Allow -Program $p -Profile Any -Enabled True -ErrorAction Stop | Out-Null; exit 0 }' +
    ' catch { exit 2 }';
end;

procedure PpFwWarn();
begin
  SuppressibleMsgBox(
    'PinPoint Studio is installed, but Windows Firewall was not updated, so a phone may ' +
    'not be able to pair on this network.' + #13#10#13#10 +
    'Run this installer again and choose Yes when Windows asks to allow changes.',
    mbInformation, MB_OK, IDOK);
end;

<event('CurStepChanged')>
procedure PpFwCurStepChanged(CurStep: TSetupStep);
var
  Exe: String;
  Code: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;
  Exe := ExpandConstant('{app}\bin\PinPointStudio.exe');
  if not FileExists(Exe) then
    Exit;
  if PpFwRuleIsGood(Exe) then begin
    Log('Firewall: inbound allow rule present for ' + Exe);
    Exit;
  end;

  Log('Firewall: no usable inbound allow rule for ' + Exe + '; elevating to add it');
  WizardForm.StatusLabel.Caption := 'Allowing PinPoint Studio through Windows Firewall...';
  if not ShellExec('runas', PpFwPowerShell(), PpFwArgs(PpFwFixBody(Exe)), '',
                   SW_HIDE, ewWaitUntilTerminated, Code) then begin
    { 1223 = ERROR_CANCELLED: the user said No to UAC. }
    Log('Firewall: elevation failed or declined (' + IntToStr(Code) + ': ' + SysErrorMessage(Code) + ')');
    PpFwWarn();
    Exit;
  end;

  if PpFwRuleIsGood(Exe) then
    Log('Firewall: inbound allow rule added (exit ' + IntToStr(Code) + ')')
  else begin
    Log('Firewall: rule still missing after the elevated fix (exit ' + IntToStr(Code) + ')');
    PpFwWarn();
  end;
end;
