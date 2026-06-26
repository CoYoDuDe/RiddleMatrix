using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Windows.Forms;

internal static class RiddleMatrixWindowsManagerLauncher
{
    private struct PayloadFile
    {
        public PayloadFile(string resourceName, string relativePath)
        {
            ResourceName = resourceName;
            RelativePath = relativePath;
        }

        public readonly string ResourceName;
        public readonly string RelativePath;
    }

    private static readonly PayloadFile[] PayloadFiles =
    {
        new PayloadFile("payload.WindowsTool.Start-RiddleMatrixWindowsManager.ps1", @"WindowsTool\Start-RiddleMatrixWindowsManager.ps1"),
        new PayloadFile("payload.USBStick-Setup.files.usr.local.bin.webserver.py", @"USBStick-Setup\files\usr\local\bin\webserver.py"),
        new PayloadFile("payload.USBStick-Setup.files.usr.local.etc.index.html", @"USBStick-Setup\files\usr\local\etc\index.html"),
        new PayloadFile("payload.USBStick-Setup.files.usr.local.etc.vendor.xlsx.full.min.js", @"USBStick-Setup\files\usr\local\etc\vendor\xlsx.full.min.js"),
    };

    [STAThread]
    private static int Main(string[] args)
    {
        string executablePath = Application.ExecutablePath;
        string executableDirectory = Path.GetDirectoryName(executablePath) ?? AppDomain.CurrentDomain.BaseDirectory;
        string toolRoot;
        string scriptPath;

        try
        {
            toolRoot = ResolveToolRoot(executablePath, executableDirectory);
            scriptPath = Path.Combine(toolRoot, "Start-RiddleMatrixWindowsManager.ps1");
            if (args.Length > 0 && string.Equals(args[0], "--extract-test", StringComparison.OrdinalIgnoreCase))
            {
                return File.Exists(scriptPath) ? 0 : 1;
            }
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                "Der Windows Manager konnte die eingebetteten Dateien nicht vorbereiten.\n\n" + ex.Message,
                "RiddleMatrix Windows Manager",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }

        if (!File.Exists(scriptPath))
        {
            MessageBox.Show(
                "Start-RiddleMatrixWindowsManager.ps1 wurde nicht gefunden.",
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

    private static string ResolveToolRoot(string executablePath, string executableDirectory)
    {
        string localToolRoot = executableDirectory;
        DirectoryInfo parent = Directory.GetParent(localToolRoot);
        string localRepoRoot = parent != null ? parent.FullName : localToolRoot;

        if (HasCompletePayload(localToolRoot, localRepoRoot))
        {
            return localToolRoot;
        }

        string payloadRoot = ExtractEmbeddedPayload(executablePath);
        return Path.Combine(payloadRoot, "WindowsTool");
    }

    private static bool HasCompletePayload(string toolRoot, string repoRoot)
    {
        return
            File.Exists(Path.Combine(toolRoot, "Start-RiddleMatrixWindowsManager.ps1")) &&
            File.Exists(Path.Combine(repoRoot, @"USBStick-Setup\files\usr\local\bin\webserver.py")) &&
            File.Exists(Path.Combine(repoRoot, @"USBStick-Setup\files\usr\local\etc\index.html")) &&
            File.Exists(Path.Combine(repoRoot, @"USBStick-Setup\files\usr\local\etc\vendor\xlsx.full.min.js"));
    }

    private static string ExtractEmbeddedPayload(string executablePath)
    {
        string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
        string versionKey = File.GetLastWriteTimeUtc(executablePath).Ticks.ToString("x");
        string payloadRoot = Path.Combine(appData, "RiddleMatrixWindowsManager", "StandalonePayload", versionKey);
        string markerPath = Path.Combine(payloadRoot, ".complete");

        if (File.Exists(markerPath) && HasCompletePayload(Path.Combine(payloadRoot, "WindowsTool"), payloadRoot))
        {
            return payloadRoot;
        }

        Directory.CreateDirectory(payloadRoot);
        Assembly assembly = Assembly.GetExecutingAssembly();

        foreach (PayloadFile file in PayloadFiles)
        {
            string targetPath = Path.Combine(payloadRoot, file.RelativePath);
            string targetDirectory = Path.GetDirectoryName(targetPath);
            if (!string.IsNullOrEmpty(targetDirectory))
            {
                Directory.CreateDirectory(targetDirectory);
            }

            using (Stream source = assembly.GetManifestResourceStream(file.ResourceName))
            {
                if (source == null)
                {
                    throw new FileNotFoundException("Eingebettete Manager-Datei fehlt: " + file.ResourceName);
                }

                using (FileStream target = File.Create(targetPath))
                {
                    source.CopyTo(target);
                }
            }
        }

        File.WriteAllText(markerPath, DateTime.UtcNow.ToString("O"));
        return payloadRoot;
    }
}
