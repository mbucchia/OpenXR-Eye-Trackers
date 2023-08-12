using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Configuration;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows.Forms;

// Reference: https://www.c-sharpcorner.com/article/how-to-perform-custom-actions-and-upgrade-using-visual-studio-installer/
namespace SetupCustomActions
{
    [RunInstaller(true)]
    public partial class CustomActions : System.Configuration.Install.Installer
    {
        public CustomActions()
        {
        }

        private class WindowWrapper : IWin32Window
        {
            private readonly IntPtr hwnd;

            public IntPtr Handle
            {
                get { return hwnd; }
            }

            public WindowWrapper(IntPtr handle)
            {
                hwnd = handle;
            }
        }

        protected override void OnAfterInstall(IDictionary savedState)
        {
            var installPath = Path.GetDirectoryName(base.Context.Parameters["AssemblyPath"]);

            // https://stackoverflow.com/questions/6213498/custom-installer-in-net-showing-form-behind-installer
            var proc = Process.GetProcessesByName("msiexec").FirstOrDefault(p => p.MainWindowTitle == "OpenXR-Eye-Trackers");
            var owner = proc != null ? new WindowWrapper(proc.MainWindowHandle) : null;

            // We want to add our layer at the very beginning, so that any other layer like the OpenXR Toolkit layer is following us.
            // We delete all entries, create our own, and recreate all entries.

            Microsoft.Win32.RegistryKey key;
            {
                key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey("SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
                var jsonPath = installPath + "\\openxr-api-layer.json";

                var existingValues = key.GetValueNames();

                ReOrderApiLayers(key, jsonPath);

                key.Close();
            }

            base.OnAfterInstall(savedState);
        }

        private void ReOrderApiLayers(Microsoft.Win32.RegistryKey key, string jsonPath)
        {
            var existingValues = key.GetValueNames();
            var entriesValues = new Dictionary<string, object>();
            foreach (var value in existingValues)
            {
                entriesValues.Add(value, key.GetValue(value));
                key.DeleteValue(value);
            }

            foreach (var value in existingValues)
            {
                // Do not re-create our own key. We will do it after this loop.
                if (value == jsonPath)
                {
                    continue;
                }

                key.SetValue(value, entriesValues[value]);
            }

            // Add ourselves last, so that all other API layers may use our functionality.
            key.SetValue(jsonPath, 0);
        }
    }
}
