# Synthetic input for ONE game window (no focus needed): mouse clicks and key presses are posted as window messages.
#   wininput.ps1 -Seat 0 -Click 5           5 left clicks (attack)      -Right to use the right button
#   wininput.ps1 -Seat 0 -Key 0x20          one key (virtual-key code)  -Hold <ms> keeps buttons / keys down that long
param([int]$Seat = 0, [int]$Click = 0, [int]$Key = 0, [switch]$Right, [int]$Hold = 120, [int]$Gap = 350)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WI {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static IntPtr Find(uint pid) { IntPtr found = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r; if (p == pid && IsWindowVisible(h) && GetClientRect(h, out r) && r.R > 200) { found = h; return false; } return true; }, IntPtr.Zero); return found; }
}
"@
$q = Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$Seat(\s|$)" } | Select-Object -First 1
if (-not $q) { throw "no game with seat $Seat" }
$h = [WI]::Find([uint32]$q.ProcessId); if ($h -eq [IntPtr]::Zero) { throw "no window" }
$r = New-Object WI+RECT; [void][WI]::GetClientRect($h, [ref]$r)
$pos = [IntPtr](([int]($r.B / 2) -shl 16) -bor [int]($r.R / 2))
for ($i = 0; $i -lt $Click; $i++) {
    if ($Right) { [void][WI]::PostMessage($h, 0x0204, [IntPtr]2, $pos); Start-Sleep -Milliseconds $Hold; [void][WI]::PostMessage($h, 0x0205, [IntPtr]0, $pos) }
    else { [void][WI]::PostMessage($h, 0x0201, [IntPtr]1, $pos); Start-Sleep -Milliseconds $Hold; [void][WI]::PostMessage($h, 0x0202, [IntPtr]0, $pos) }
    Start-Sleep -Milliseconds $Gap
}
if ($Key) { [void][WI]::PostMessage($h, 0x0100, [IntPtr]$Key, [IntPtr]1); Start-Sleep -Milliseconds $Hold; [void][WI]::PostMessage($h, 0x0101, [IntPtr]$Key, [IntPtr]0xC0000001) }
"sent to pid $($q.ProcessId)"
