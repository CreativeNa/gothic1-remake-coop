# Real (not posted) mouse clicks on one game window: brings it to the front, clicks N times with the left (or right) button, no cursor movement.
param([int]$Seat = 0, [int]$Click = 5, [switch]$Right, [int]$Hold = 150, [int]$Gap = 450)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class RC {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  public static IntPtr Find(uint pid) { IntPtr f = IntPtr.Zero; EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); RECT r; if (p == pid && IsWindowVisible(h) && GetWindowRect(h, out r) && r.R - r.L > 300) { f = h; return false; } return true; }, IntPtr.Zero); return f; }
}
"@
$q = Get-CimInstance Win32_Process -Filter "Name='G1R-Win64-Shipping.exe'" | Where-Object { $_.CommandLine -match "-CoopSeat=$Seat(\s|$)" } | Select-Object -First 1
$h = [RC]::Find([uint32]$q.ProcessId); [void][RC]::ShowWindow($h, 9); [void][RC]::SetForegroundWindow($h); Start-Sleep -Milliseconds 600
$r = New-Object RC+RECT; [void][RC]::GetWindowRect($h, [ref]$r); [void][RC]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2)); Start-Sleep -Milliseconds 200
$dn = if ($Right) { 0x08 } else { 0x02 }; $up = if ($Right) { 0x10 } else { 0x04 }
for ($i = 0; $i -lt $Click; $i++) { [RC]::mouse_event($dn, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $Hold; [RC]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $Gap }
"clicked $Click x"
