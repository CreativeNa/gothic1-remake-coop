# Screenshot of ONE game window (by process id or seat), even when other windows cover it: PrintWindow(PW_RENDERFULLCONTENT).
#   winshot.ps1 -Seat 0 [-Out path.png]   |   winshot.ps1 -ProcessId 1234
param([int]$Seat = -1, [int]$ProcessId = 0, [string]$Out = "")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class WS {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static IntPtr Find(uint pid) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r; if (p == pid && IsWindowVisible(h) && GetClientRect(h, out r) && r.R > 200) { found = h; return false; } return true; }, IntPtr.Zero); return found; }
}
"@
if (-not $ProcessId) {
    $q = Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$Seat(\s|$)" } | Select-Object -First 1
    if (-not $q) { throw "no game with seat $Seat" }
    $ProcessId = $q.ProcessId
}
$h = [WS]::Find([uint32]$ProcessId); if ($h -eq [IntPtr]::Zero) { throw "no window for pid $ProcessId" }
$r = New-Object WS+RECT; [void][WS]::GetClientRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap $r.R, $r.B
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
[void][WS]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
if (-not $Out) { $Out = Join-Path $env:TEMP "g1shots\win_pid$ProcessId.png" }
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
$Out
