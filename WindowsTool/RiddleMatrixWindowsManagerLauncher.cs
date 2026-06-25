using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

internal static class RiddleMatrixWindowsManagerLauncher
{
    [STAThread]
    private static int Main()
    {
        string executablePath = Application.ExecutablePath;
        string toolRoot = Path.GetDirectoryName(executablePath) ?? AppDomain.CurrentDomain.BaseDirectory;
        string scriptPath = Path.Combine(toolRoot, "Start-RiddleMatrixWindowsManager.ps1");

        if (!File.Exists(scriptPath))
        {
            MessageBox.Show(
                "Start-RiddleMatrixWindowsManager.ps1 wurde neben der EXE nicht gefunden.",
                "RiddleMatrix Windows Manager",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }

        try
        {
            var startInfo = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = "-NoProfile -ExecutionPolicy Bypass -File \"" + scriptPath + "\"",
                UseShellExecute = false,
                CreateNoWindow = true,
                WorkingDirectory = toolRoot,
            };

            Process.Start(startInfo);
            return 0;
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                "Der Windows Manager konnte nicht gestartet werden.\n\n" + ex.Message,
                "RiddleMatrix Windows Manager",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }
}
