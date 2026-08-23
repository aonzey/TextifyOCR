$ErrorActionPreference = 'Stop'
$log = "E:\Textify_Project\test_steps.log"
Set-Content -Path $log -Value "start"
function Log([string]$m) { Add-Content -Path $log -Value $m; Write-Host $m }

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageS(IntPtr h, uint msg, IntPtr wp, StringBuilder lp);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
}
"@

Stop-Process -Name Textify -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
$proc = Start-Process -FilePath "E:\Textify_Project\build_x64\TextifyOCR.exe" -WorkingDirectory "E:\Textify_Project\build_x64" -PassThru
Start-Sleep -Seconds 3
Log ("proc alive: " + (-not $proc.HasExited))

# find main window by pid + class
$script:main = [IntPtr]::Zero
$cb = [W+EnumProc]{ param($h, $lp)
    $p = 0
    [W]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
    if ($p -eq $targetPid) {
        $c = New-Object System.Text.StringBuilder 256
        [W]::GetClassNameW($h, $c, 256) | Out-Null
        if ($c.ToString() -eq "Textify") { $script:main = $h; return $false }
    }
    return $true
}
$targetPid = $proc.Id
[W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Log ("main: " + $script:main)

Log "posting WM_COMMAND IDC_SHOW_INI..."
[W]::PostMessage($script:main, 0x0111, [IntPtr]1008, [IntPtr]::Zero)
Log "WM_COMMAND posted"

Start-Sleep -Seconds 2
Log "slept 2s"

# find settings dialog
$script:settings = [IntPtr]::Zero
$enumCb = [W+EnumProc]{ param($h, $lp)
    $sb = New-Object System.Text.StringBuilder 256
    [W]::GetClassNameW($h, $sb, 256) | Out-Null
    if ($sb.ToString() -eq "#32770") {
        $owner = [W]::GetWindow($h, 4)
        if ($owner -eq $script:main) { $script:settings = $h; return $false }
    }
    return $true
}
[W]::EnumWindows($enumCb, [IntPtr]::Zero) | Out-Null
Log ("settings: " + $script:settings)

if ($script:settings -ne [IntPtr]::Zero) {
    $edit = [W]::GetDlgItem($script:settings, 1010)
    Log ("edit ctrl: " + $edit)
    $len = [W]::SendMessage($edit, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    Log ("text len: " + $len)
    $sb = New-Object System.Text.StringBuilder ($len + 16)
    [W]::SendMessageS($edit, 0x000D, [IntPtr]($len + 16), $sb) | Out-Null
    $text = $sb.ToString()
    $crlf = ([regex]::Matches($text, "`r`n")).Count
    $hasChinese = $text.Contains([char]0x914D)
    Log ("len=" + $text.Length + " CRLF=" + $crlf + " Chinese=" + $hasChinese)
    $fileText = [System.IO.File]::ReadAllText("E:\Textify_Project\build_x64\Textify.ini", [System.Text.Encoding]::Unicode)
    Log ("equals file: " + ($text.Trim() -eq $fileText.Trim()))
    [W]::SendMessage($script:settings, 0x0112, [IntPtr]::Zero, [IntPtr]::Zero)
    Log "closed dialog"
}

Stop-Process -Name Textify -Force -ErrorAction SilentlyContinue
Log "done"
