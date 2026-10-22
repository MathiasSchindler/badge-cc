param(
    [int]$Seconds = 60,
    [string]$Address = '28CDC1180C60',
    [ValidateSet('buttons','reset')]
    [string]$Mode = 'buttons'
)
$ErrorActionPreference = 'Stop'
if ($Seconds -lt 10 -or $Seconds -gt 180) { throw 'Seconds must be in 10..180' }
$Address = $Address.Replace(':','').Replace('-','').ToUpperInvariant()
if ($Address -notmatch '^[0-9A-F]{12}$') { throw 'Expected Bluetooth address must contain 12 hexadecimal digits' }

Add-Type -ReferencedAssemblies 'System.dll','System.Core.dll','System.Windows.Forms.dll','System.Drawing.dll' -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

public sealed class ClickerKeyObserver : Form {
    [StructLayout(LayoutKind.Sequential)]
    private struct Device { public ushort Page, Usage; public uint Flags; public IntPtr Target; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Header { public uint Type, Size; public IntPtr Device, Parameter; }
    [StructLayout(LayoutKind.Sequential)]
    private struct Keyboard {
        public ushort MakeCode, Flags, Reserved, Key;
        public uint Message, Extra;
    }
    [DllImport("user32.dll", SetLastError=true)]
    private static extern bool RegisterRawInputDevices(Device[] devices, uint count, uint size);
    [DllImport("user32.dll", SetLastError=true)]
    private static extern uint GetRawInputData(IntPtr raw, uint command, IntPtr data, ref uint size, uint headerSize);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern uint GetRawInputDeviceInfo(IntPtr device, uint command, StringBuilder data, ref uint size);
    private readonly List<string> lines = new List<string>();
    private readonly Dictionary<IntPtr,bool> devices = new Dictionary<IntPtr,bool>();
    private readonly TextBox text = new TextBox();
    private readonly string address;
    private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
    private string failure;

    private ClickerKeyObserver(int seconds, string expected, string mode) {
        address=expected;
        Text="GitHub badge keyboard delivery test";
        Width=780; Height=450;
        text.Multiline=true; text.ReadOnly=true; text.Dock=DockStyle.Fill;
        text.Font=new Font("Consolas",11);
        text.Text=mode=="reset"?
                  "Hold C/Right, then press RESET while still holding it.\r\n"+
                  "Keep holding until CONNECTED - READY, then release it.\r\n"+
                  "Finally press A/Left once. No new pairing should be needed.\r\n\r\n":
                  "Press A, B, C, Up, Down once each, with pauses.\r\n"+
                  "Then hold C for about two seconds and release it.\r\n"+
                  "2026: Left, Enter, Right, Up, Down; then hold Right.\r\n"+
                  "Only this badge's Page Up, Page Down and B events are recorded.\r\n\r\n";
        Controls.Add(text);
        Shown += delegate {
            Device[] registration={new Device {Page=1,Usage=6,Flags=0x100,Target=Handle}};
            if (!RegisterRawInputDevices(registration,1,(uint)Marshal.SizeOf(typeof(Device)))) {
                failure="Raw keyboard registration failed: "+Marshal.GetLastWin32Error(); Close();
            }
        };
        timer.Interval=seconds*1000;timer.Tick+=delegate {Close();};timer.Start();
    }
    private bool TargetDevice(IntPtr handle) {
        bool match;
        if (devices.TryGetValue(handle,out match)) return match;
        uint capacity=2048;StringBuilder name=new StringBuilder((int)capacity);
        if (GetRawInputDeviceInfo(handle,0x20000007,name,ref capacity)==UInt32.MaxValue)
            throw new InvalidOperationException("Raw device identity query failed: "+Marshal.GetLastWin32Error());
        string path=name.ToString().ToUpperInvariant();
        match=path.Contains(address) && path.Contains("CAFE") && path.Contains("4027");
        devices.Add(handle,match);return match;
    }
    protected override void WndProc(ref Message message) {
        if (message.Msg==0x00ff && failure==null) {
            try {
                uint size=0, headerSize=(uint)Marshal.SizeOf(typeof(Header));
                if (GetRawInputData(message.LParam,0x10000003,IntPtr.Zero,ref size,headerSize)==UInt32.MaxValue ||
                    size<headerSize || size>65536)
                    throw new InvalidOperationException("Invalid raw input size");
                IntPtr memory=Marshal.AllocHGlobal((int)size);
                try {
                    if (GetRawInputData(message.LParam,0x10000003,memory,ref size,headerSize)!=size)
                        throw new InvalidOperationException("Raw input read failed");
                    Header header=(Header)Marshal.PtrToStructure(memory,typeof(Header));
                    if (header.Type==1 && TargetDevice(header.Device)) {
                        if (size<headerSize+Marshal.SizeOf(typeof(Keyboard)))
                            throw new InvalidOperationException("Truncated raw keyboard event");
                        Keyboard key=(Keyboard)Marshal.PtrToStructure(IntPtr.Add(memory,(int)headerSize),typeof(Keyboard));
                        if (key.Key==0x21 || key.Key==0x22 || key.Key==0x42) {
                            string name=key.Key==0x21?"PAGE_UP":key.Key==0x22?"PAGE_DOWN":"B";
                            string line=String.Format("{0:o} {1} {2}",DateTimeOffset.Now,name,(key.Flags&1)!=0?"UP":"DOWN");
                            lines.Add(line);text.AppendText(line+"\r\n");
                        }
                    }
                } finally {Marshal.FreeHGlobal(memory);}
            } catch (InvalidOperationException error) {
                failure=error.Message;
                BeginInvoke(new Action(Close));
            }
        }
        base.WndProc(ref message);
    }
    public static string[] Run(int seconds,string address,string mode) {
        using (ClickerKeyObserver form=new ClickerKeyObserver(seconds,address,mode)) {
            Application.Run(form);form.timer.Stop();form.timer.Dispose();
            if (form.failure!=null) throw new InvalidOperationException(form.failure);
            return form.lines.ToArray();
        }
    }
}
'@

$events = [ClickerKeyObserver]::Run($Seconds,$Address,$Mode)
$events | Set-Content -LiteralPath "$PSScriptRoot\..\build\clicker\keyboard-$Mode-observation.txt"
$expected = @(
    'PAGE_UP DOWN','PAGE_UP UP','B DOWN','B UP','PAGE_DOWN DOWN','PAGE_DOWN UP',
    'PAGE_UP DOWN','PAGE_UP UP','PAGE_DOWN DOWN','PAGE_DOWN UP','PAGE_DOWN DOWN','PAGE_DOWN UP'
)
if ($Mode -eq 'reset') { $expected=@('PAGE_DOWN DOWN','PAGE_DOWN UP','PAGE_UP DOWN','PAGE_UP UP') }
$observed = @($events | ForEach-Object { ($_ -split ' ',2)[1] })
Write-Output "Captured $($observed.Count) badge-only keyboard events."
$events | Write-Output
if ($observed.Count -ne $expected.Count) { throw "Expected exactly $($expected.Count) key transition events" }
for ($i=0; $i -lt $expected.Count; ++$i) {
    if ($observed[$i] -ne $expected[$i]) { throw "Wrong key/release order at event $i" }
}
if ($Mode -eq 'reset') { Write-Output 'PASS: RESET reconnect and held-at-start suppression, followed by a fresh key tap.' }
else { Write-Output 'PASS: all five controls, explicit key releases, and no repeat during held C/Right.' }
