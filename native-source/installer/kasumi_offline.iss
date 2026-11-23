var
  TrustChecked, SignedServerTrusted: Boolean;

function TrustsSignedServer: Boolean;
var
  ExitCode: Integer;
begin
  if not TrustChecked then
  begin
    TrustChecked := True;
    if RegKeyExists(HKLM64, 'SOFTWARE\Microsoft\SystemCertificates\ROOT\Certificates\B5931A606CE74754EC0F5C8B5F0518B168D7B999') then
    begin
      ExtractTemporaryFile('KasumiSignedServer.exe');
      SignedServerTrusted := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
        '-NoProfile -NonInteractive -Command "if ((Get-AuthenticodeSignature -LiteralPath ''.\KasumiSignedServer.exe'').Status -eq ''Valid'') { exit 0 }; exit 1"',
        ExpandConstant('{tmp}'), SW_HIDE, ewWaitUntilTerminated, ExitCode) and (ExitCode = 0);
    end;
  end;
  Result := SignedServerTrusted;
end;

function InstallBundledRuntime(var NeedsRestart: Boolean): String;
var
  ExitCode: Integer;
begin
  Result := '';
  if VCRuntimeIsInstalled then exit;
  ExtractTemporaryFile('VC_redist.x64.exe');
  if not Exec(ExpandConstant('{tmp}\VC_redist.x64.exe'), '/install /quiet /norestart',
      '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then
    Result := '无法启动安装包内的 Microsoft Visual C++ x64 运行库。'
  else if ExitCode = 3010 then
    NeedsRestart := True
  else if (ExitCode <> 0) and (ExitCode <> 1638) then
    Result := 'Microsoft Visual C++ x64 运行库安装失败。退出码 ' + IntToStr(ExitCode);
  if (Result = '') and not VCRuntimeIsInstalled then
    Result := '请重启 Windows 后重新运行此安装程序以完成运行库安装。';
end;
