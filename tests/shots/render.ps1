<#
Renders the scenes in scenes.txt on a hidden Win32 desktop, into a folder of PNGs. A refactoring
renders before and after, then compare.ps1 proves that nothing moved. Images depend on the machine
(fonts, driver), so they are not committed.

  tests/shots/render.ps1 -Exe build-msvc/apps/gui/crpp.exe -Out <folder> [-Dpi 144] [-Only home,chat] [-Extra '--theme','messenger','--locale','en']
#>
param(
  [Parameter(Mandatory)][string]$Exe,
  [Parameter(Mandatory)][string]$Out,
  [string]$Size = '951x692',
  [int]$Hour = 14,
  [int]$Dpi = 96,
  [string[]]$Extra = @(),
  [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CrppHiddenDesktop {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct STARTUPINFO {
    public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
    public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    public short wShowWindow, cbReserved2;
    public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
  }
  [StructLayout(LayoutKind.Sequential)] public struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int pid, tid; }
  [DllImport("user32", CharSet = CharSet.Unicode, SetLastError = true)]
  public static extern IntPtr CreateDesktopW(string name, IntPtr device, IntPtr mode, int flags, uint access, IntPtr attributes);
  [DllImport("user32")] public static extern bool CloseDesktop(IntPtr desktop);
  [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
  static extern bool CreateProcessW(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
  [DllImport("kernel32")] static extern uint WaitForSingleObject(IntPtr handle, uint ms);
  [DllImport("kernel32")] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
  [DllImport("kernel32")] static extern bool TerminateProcess(IntPtr process, uint code);
  [DllImport("kernel32")] static extern bool CloseHandle(IntPtr handle);
  public static int Run(string desktop, string exe, string args, string cwd) {
    var si = new STARTUPINFO(); si.cb = Marshal.SizeOf(si); si.lpDesktop = desktop;
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, "\"" + exe + "\" " + args, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, cwd, ref si, out pi)) return -2;
    bool hung = WaitForSingleObject(pi.hProcess, 30000) != 0;
    if (hung) TerminateProcess(pi.hProcess, 1);
    uint code; GetExitCodeProcess(pi.hProcess, out code);
    if (hung) code = unchecked((uint)-3);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return (int)code;
  }
}
'@
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$data = Join-Path $Out '.data'
New-Item -ItemType Directory -Force $data | Out-Null
$desktop = 'crpp-shots'
$handle = [CrppHiddenDesktop]::CreateDesktopW($desktop, [IntPtr]::Zero, [IntPtr]::Zero, 0, 0x10000000, [IntPtr]::Zero)
if ($handle -eq [IntPtr]::Zero) { throw 'CreateDesktop failed' }
$failed = 0
try {
  foreach ($line in Get-Content (Join-Path $PSScriptRoot 'scenes.txt')) {
    if (-not $line -or $line.StartsWith('#')) { continue }
    $words = $line -split ' '
    if ($Only -and $words[0] -notin $Only) { continue }
    $png = Join-Path $Out "$($words[0]).png"
    # Options are read from the end of the command line; --data must stay last.
    $options = @()
    if ($Dpi -ne 96) { $options += '--dpi', $Dpi }
    $options += $Extra
    $command = @('--shot', "`"$png`"", $Size, $Hour) + $words + $options + @('--data', "`"$data`"")
    $code = [CrppHiddenDesktop]::Run($desktop, $Exe, ($command -join ' '), (Split-Path $Exe))
    # Either of these would be the same for every scene left, so going on only costs time.
    if ($code -eq -3) { throw "$line did not finish in 30 s" }
    if ($code -eq 3) { throw "$line could not start: $(Get-Content -Raw "$png.error" -ErrorAction SilentlyContinue)" }
    if ($code -ne 0) { Write-Output "FAILED $line -> $code"; $failed++ }
  }
} finally {
  [CrppHiddenDesktop]::CloseDesktop($handle) | Out-Null
}
Write-Output "rendered into $Out, $failed failed"
if ($failed) { exit 1 }
