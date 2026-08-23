# Textify end-to-end test v3 (ASCII only)
$ErrorActionPreference = 'Stop'
$log = "E:\Textify_Project\test_steps.log"
Set-Content -Path $log -Value "start"
function Log([string]$m) {
    Add-Content -Path $log -Value $m
    Write-Host $m
}
Log "script begin"
try {

Stop-Process -Name Textify -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Log "old instances killed"

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W {
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll", EntryPoint="SendMessageW", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageS(IntPtr h, uint msg, IntPtr wp, StringBuilder lp);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    public delegate bool EnumProc(IntPtr h, IntPtr lp);
}
"@

function Find-WindowByPid([int]$targetPid, [string]$classPattern) {
    $script:found = [IntPtr]::Zero
    $cb = [W+EnumProc]{ param($h, $lp)
        $p = 0
        [W]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
        if ($p -eq $targetPid) {
            $c = New-Object System.Text.StringBuilder 256
            [W]::GetClassNameW($h, $c, 256) | Out-Null
            if ($c.ToString() -like $classPattern) { $script:found = $h; return $false }
        }
        return $true
    }
    [W]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
    return $script:found
}

Stop-Process -Name Textify -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

$exe = "E:\Textify_Project\build_x64\TextifyOCR.exe"
$proc = Start-Process -FilePath $exe -WorkingDirectory "E:\Textify_Project\build_x64" -PassThru
Start-Sleep -Seconds 3
Log ("[1] process alive: " + (-not $proc.HasExited))

$main = Find-WindowByPid $proc.Id "Textify"
Log ("[2] main window: " + $main)

# --- Test A: More settings dialog shows ini correctly ---
if ($main -ne [IntPtr]::Zero) {
    [W]::PostMessage($main, 0x0111, [IntPtr]1008, [IntPtr]::Zero)  # WM_COMMAND IDC_SHOW_INI
    Start-Sleep -Seconds 2

    $script:settings = [IntPtr]::Zero
    $enumCb = [W+EnumProc]{ param($h, $lp)
        $sb = New-Object System.Text.StringBuilder 256
        [W]::GetClassNameW($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq "#32770") {
            $owner = [W]::GetWindow($h, 4)  # GW_OWNER
            if ($owner -eq $main) { $script:settings = $h; return $false }
        }
        return $true
    }
    [W]::EnumWindows($enumCb, [IntPtr]::Zero) | Out-Null
    Log ("[3] settings dialog: " + $script:settings)

    if ($script:settings -ne [IntPtr]::Zero) {
        $edit = [W]::GetDlgItem($script:settings, 1010)  # IDC_CONFIG_TEXT
        $len = [W]::SendMessage($edit, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()  # WM_GETTEXTLENGTH
        $sb = New-Object System.Text.StringBuilder ($len + 16)
        [W]::SendMessageS($edit, 0x000D, [IntPtr]($len + 16), $sb) | Out-Null  # WM_GETTEXT
        $text = $sb.ToString()
        $crlf = ([regex]::Matches($text, "`r`n")).Count
        $loneLf = ([regex]::Matches($text, "(?<!`r)`n")).Count
        Log ("[4] edit text length: " + $text.Length + ", CRLF breaks: " + $crlf + ", lone LF: " + $loneLf)
        $fileText = [System.IO.File]::ReadAllText("E:\Textify_Project\build_x64\Textify.ini", [System.Text.Encoding]::Unicode)
        Log ("[5] text equals ini file: " + ($text.Trim() -eq $fileText.Trim()))
        $hasChinese = $text.Contains([char]0x914D)  # char from Chinese comment
        Log ("[6] Chinese comments readable: " + $hasChinese)
        [W]::PostMessage($script:settings, 0x0112, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_CLOSE
        Start-Sleep -Seconds 1
    } else {
        Log "[3] SETTINGS DIALOG NOT FOUND"
    }
} else {
    Log "[2] MAIN WINDOW NOT FOUND"
}

# --- Test B: OCR hotkey + drag selection ---
Add-Type -AssemblyName System.Drawing
function Get-ScreenPixel([int]$x, [int]$y) {
    $bmp = New-Object System.Drawing.Bitmap 1,1
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size 1,1))
    $g.Dispose()
    $c = $bmp.GetPixel(0,0)
    $bmp.Dispose()
    return $c
}
function Scan-RegionForColor([int]$x1, [int]$y1, [int]$x2, [int]$y2, [int]$r, [int]$g2, [int]$b) {
    $w = $x2 - $x1; $h = $y2 - $y1
    if ($w -le 0 -or $h -le 0) { return $false }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x1, $y1, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $found = $false
    for ($yy = 0; $yy -lt $h -and -not $found; $yy++) {
        for ($xx = 0; $xx -lt $w; $xx++) {
            $c = $bmp.GetPixel($xx, $yy)
            if ([Math]::Abs($c.R - $r) -le 10 -and [Math]::Abs($c.G - $g2) -le 10 -and [Math]::Abs($c.B - $b) -le 10) { $found = $true; break }
        }
    }
    $bmp.Dispose()
    return $found
}

# baseline pixels BEFORE the overlay appears (screen must be static here)
$baseOutside = Get-ScreenPixel 100 100    # far from the selection
$baseInside  = Get-ScreenPixel 500 400    # will be inside the selection
Log ("[6b] baseline pixels - outside(100,100): RGB(" + $baseOutside.R + "," + $baseOutside.G + "," + $baseOutside.B + ") inside(500,400): RGB(" + $baseInside.R + "," + $baseInside.G + "," + $baseInside.B + ")")

[W]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)      # Ctrl down
[W]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)      # Shift down
[W]::keybd_event(0x4F, 0, 0, [UIntPtr]::Zero)      # O down
[W]::keybd_event(0x4F, 0, 2, [UIntPtr]::Zero)      # O up
[W]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero)      # Shift up
[W]::keybd_event(0x11, 0, 2, [UIntPtr]::Zero)      # Ctrl up
Start-Sleep -Seconds 2

$overlay = Find-WindowByPid $proc.Id "ATL:*"
Log ("[7] OCR overlay window: " + $overlay)

# verify the dim overlay is actually rendered: pixel outside the future
# selection must be darker than the baseline (~57% brightness with alpha 110)
if ($overlay -ne [IntPtr]::Zero) {
    $dimPx = Get-ScreenPixel 100 100
    $dimRatio = 1.0
    if (($baseOutside.R + $baseOutside.G + $baseOutside.B) -gt 0) {
        $dimRatio = ($dimPx.R + $dimPx.G + $dimPx.B) / [double]($baseOutside.R + $baseOutside.G + $baseOutside.B)
    }
    Log ("[7b] dim overlay pixel RGB(" + $dimPx.R + "," + $dimPx.G + "," + $dimPx.B + ") brightness ratio: " + [Math]::Round($dimRatio, 2) + " (expect ~0.57, NOT 1.0/black)")
}

# drag from (300,300) to (700,500)
[W]::SetCursorPos(300, 300) | Out-Null
Start-Sleep -Milliseconds 300
[W]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)      # LEFTDOWN
for ($i = 1; $i -le 20; $i++) {
    [W]::SetCursorPos((300 + 20 * $i), (300 + 10 * $i)) | Out-Null
    Start-Sleep -Milliseconds 30
}
# mid-drag rendering checks:
#  - blue selection border RGB(0,150,255) must be visible around the rect
#  - interior (500,400) must be restored to original brightness
if ($overlay -ne [IntPtr]::Zero) {
    $hasBlue = Scan-RegionForColor 295 295 705 505 0 150 255
    Log ("[8b] blue selection border rendered: " + $hasBlue)
    $inPx = Get-ScreenPixel 500 400
    $insideRestored = ([Math]::Abs($inPx.R - $baseInside.R) -le 6 -and [Math]::Abs($inPx.G - $baseInside.G) -le 6 -and [Math]::Abs($inPx.B - $baseInside.B) -le 6)
    Log ("[8c] selection interior restored (RGB " + $inPx.R + "," + $inPx.G + "," + $inPx.B + " vs base " + $baseInside.R + "," + $baseInside.G + "," + $baseInside.B + "): " + $insideRestored)
}
[W]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)      # LEFTUP
Log "[8] drag done, polling for OCR result window..."

$result = [IntPtr]::Zero
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    $result = Find-WindowByPid $proc.Id "TextifyEditDlg"
    if ($result -ne [IntPtr]::Zero) { break }
}
Log ("[9] OCR result window: " + $(if ($result -ne [IntPtr]::Zero) { "FOUND (after ~" + (2*($i+1)) + "s)" } else { "NOT FOUND" }))
if ($result -ne [IntPtr]::Zero) {
    $edit = [W]::GetDlgItem($result, 1011)  # IDC_EDIT
    $len = [W]::SendMessage($edit, 0x000E, [IntPtr]::Zero, [IntPtr]::Zero).ToInt32()
    $sb = New-Object System.Text.StringBuilder ($len + 16)
    [W]::SendMessageS($edit, 0x000D, [IntPtr]($len + 16), $sb) | Out-Null
    $t = $sb.ToString()
    Log ("[10] OCR result (" + $t.Length + " chars): " + $t.Substring(0, [Math]::Min(150, $t.Length)).Replace("`r`n", " | "))
}

Stop-Process -Name Textify -Force -ErrorAction SilentlyContinue
Log "[11] test finished"

} catch {
    Log ("FATAL: " + $_.Exception.Message)
    Log ($_.ScriptStackTrace)
    Stop-Process -Name Textify -Force -ErrorAction SilentlyContinue
}
