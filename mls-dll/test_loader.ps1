# 32-bit host harness for mls-sim.dll.
# Run with: C:\Windows\SysWOW64\WindowsPowerShell\v1.0\powershell.exe -NoProfile -File test_loader.ps1
# Output goes to whatever redirects stdout/stderr from the parent (cmd /c).

$env:MLS_AUTO_TEST = '1'

Add-Type -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32", SetLastError = true)]
public static extern System.IntPtr LoadLibraryA(string name);
[System.Runtime.InteropServices.DllImport("kernel32")]
public static extern uint GetLastError();
'@ -Name K -Namespace W

$dll = 'D:\war3Project\KKYDWE\plugin\mls-sim.dll'
Write-Host "[harness] LoadLibraryA $dll"
$h = [W.K]::LoadLibraryA($dll)
if ($h -eq [System.IntPtr]::Zero) {
    $err = [W.K]::GetLastError()
    Write-Host "[harness] LoadLibraryA FAILED, GetLastError=$err"
    exit 1
}
Write-Host "[harness] loaded, handle=$h. sleeping 16s while DLL runs auto_test..."
Start-Sleep -Seconds 16
Write-Host "[harness] done, exiting (worker thread dies with process)"
