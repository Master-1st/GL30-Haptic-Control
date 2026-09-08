using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Windows.Forms;
using System.Windows.Forms.DataVisualization.Charting;

namespace Gl30.EncoderViewer
{
  public sealed class StatusSample
  {
    public Dictionary<string, long> Fields { get; private set; }
    public double AngleDegrees { get; private set; }
    public bool EncoderValid { get; private set; }
    public bool OutputOff { get; private set; }
    public int RawEncoder { get; private set; }
    public long Diagnostic { get; private set; }

    private StatusSample(Dictionary<string, long> fields)
    {
      Fields = fields;
      RawEncoder = (int)fields["enc"];
      Diagnostic = fields["diag"];
      AngleDegrees = (double)RawEncoder * (360.0 / 16384.0);
      long mode = fields["mode"];
      long fault = fields["fault"];
      long moe = fields["moe"];
      long off = fields["off"];
      long calibrated = fields["calibrated"];
      long button = fields["button"];
      long health = fields["health"];
      long age = fields["age_us"];
      long diag = fields["diag"];
      OutputOff = (mode == 0L) && (fault == 0L) && (moe == 0L) && (off == 1L) && (calibrated == 0L) && (button == 0L);
      EncoderValid = ((health & 0x2L) != 0L) && (age >= 0L) && (age <= 750L) && ((diag & 0xF00L) == 0x100L);
    }

    public static bool TryParse(string line, out StatusSample sample, out string error)
    {
      sample = null;
      error = null;

      if (string.IsNullOrWhiteSpace(line))
      {
        error = "空行";
        return false;
      }

      line = line.Trim();
      if (!line.StartsWith("STATUS ", StringComparison.Ordinal))
      {
        error = "非STATUS行";
        return false;
      }

      string[] tokens = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
      if (tokens.Length < 2)
      {
        error = "STATUS字段不足";
        return false;
      }

      var fields = new Dictionary<string, long>(StringComparer.OrdinalIgnoreCase);
      for (int i = 1; i < tokens.Length; ++i)
      {
        string token = tokens[i];
        int eq = token.IndexOf('=');
        if (eq <= 0 || eq >= token.Length - 1)
        {
          error = "字段缺失: " + token;
          return false;
        }

        string key = token.Substring(0, eq);
        string rawValue = token.Substring(eq + 1);
        long value;
        if (!long.TryParse(rawValue, NumberStyles.Integer, CultureInfo.InvariantCulture, out value))
        {
          error = "字段非法: " + token;
          return false;
        }

        if (fields.ContainsKey(key))
        {
          error = "重复字段: " + key;
          return false;
        }

        fields.Add(key, value);
      }

      string[] required = {
        "enc",
        "diag",
        "age_us",
        "health",
        "mode",
        "fault",
        "moe",
        "off",
        "button",
        "calibrated",
        "enc_ok",
        "enc_err",
        "uart_err",
        "adc_bad",
        "deadline",
        "self_left",
        "self_fail",
        "self_max",
        "isr_max",
        "drv"
      };
      for (int i = 0; i < required.Length; ++i)
      {
        if (!fields.ContainsKey(required[i]))
        {
          error = "缺少" + required[i];
          return false;
        }
        // 仅对状态/计数字段做非负约束，其他ADC/电流场由固件定义允许带符号值。
        if (fields[required[i]] < 0L)
        {
          error = "字段非法(负数): " + required[i] + "=" + fields[required[i]];
          return false;
        }
      }

      if (fields["diag"] > 65535L)
      {
        error = "diag超过65535范围: " + fields["diag"];
        return false;
      }

      long enc = fields["enc"];
      if (enc < 0L || enc > 16383L)
      {
        error = "enc范围外: " + enc;
        return false;
      }

      sample = new StatusSample(fields);
      return true;
    }
  }

  public sealed class AngleTracker
  {
    private const double DegPerCount = 360.0 / 16384.0;
    private const int CountsPerRevolution = 16384;
    private const int HalfRevolution = CountsPerRevolution / 2;

    private bool _hasPoint;
    private int _prevRaw;
    private long _prevEncoderOk;
    private double _relativeDegrees;

    public double RelativeDegrees
    {
      get { return _relativeDegrees; }
    }

    // deltaSeconds: 与上一条有效STATUS样本之间的时间间隔（秒）。
    public bool Update(int raw, double deltaSeconds, long encoderOk)
    {
      if (raw < 0 || raw > 16383)
      {
        throw new ArgumentOutOfRangeException("raw");
      }
      if (double.IsNaN(deltaSeconds) || double.IsInfinity(deltaSeconds))
      {
        _hasPoint = true;
        _relativeDegrees = 0.0;
        _prevRaw = raw;
        _prevEncoderOk = encoderOk;
        return false;
      }

      if (!_hasPoint)
      {
        _hasPoint = true;
        _prevRaw = raw;
        _prevEncoderOk = encoderOk;
        _relativeDegrees = 0.0;
        return false;
      }

      if (deltaSeconds <= 0.0 || deltaSeconds > 0.5 || encoderOk < _prevEncoderOk)
      {
        _relativeDegrees = 0.0;
        _prevRaw = raw;
        _prevEncoderOk = encoderOk;
        return false;
      }

      int delta = raw - _prevRaw;
      if (delta > HalfRevolution)
      {
        delta -= CountsPerRevolution;
      }
      else if (delta < -HalfRevolution)
      {
        delta += CountsPerRevolution;
      }

      _relativeDegrees += delta * DegPerCount;
      _prevRaw = raw;
      _prevEncoderOk = encoderOk;
      return true;
    }

    public void Reset()
    {
      _hasPoint = false;
      _prevRaw = 0;
      _prevEncoderOk = 0L;
      _relativeDegrees = 0.0;
    }
  }

  internal sealed class SerialPortStatus
  {
    public bool IsConnected;
    public bool IsStopping;
    public string HealthReason = string.Empty;
    public string Port;
    public DateTime ConnectTimeUtc;
  }

  internal sealed class ViewerOptions
  {
    public string PortName;
    public string OutputDirectory;
    public string CapturePath;
    public int? CloseAfterSeconds;
  }

  internal sealed class MainForm : Form
  {
    private const int PollIntervalMs = 20;
    private const int MaxChartPoints = 1000;
    private const int RollingWindowSeconds = 30;
    private const int StatusIntervalMs = 100;
    private const double StaleWarnSeconds = 1.0;
    private const int StatusTimeoutMs = 2000;
    private const int MaxRxLineBuffer = 32 * 1024;

    private readonly string _portName;
    private readonly string _outputDirectory;
    private readonly string _capturePath;
    private readonly int? _closeAfterSeconds;

    private readonly Label _lblTitle;
    private readonly Label _lblConnection;
    private readonly Label _lblWarning;
    private readonly Label _lblMode;
    private readonly Label _lblFault;
    private readonly Label _lblHealth;
    private readonly Label _lblAge;
    private readonly Label _lblMoe;
    private readonly Label _lblDrv;
    private readonly Label _lblOff;
    private readonly Label _lblDiag;
    private readonly Label _lblTiming;
    private readonly Label _lblCurrentAngle;
    private readonly Label _lblRawEncoder;
    private readonly Label _lblRelativeAngle;
    private readonly Label _lblCounters;
    private readonly Label _lblStatus;
    private readonly Label _lblParseError;
    private readonly Label _lblReason;
    private readonly Button _btnConnect;
    private readonly Button _btnDisconnect;
    private readonly CheckBox _chkPause;
    private readonly Button _btnOpenDir;
    private readonly Button _btnResetRelative;

    private readonly Chart _chartAngle;
    private readonly Chart _chartRelative;
    private readonly Series _seriesAngle;
    private readonly Series _seriesRelative;

    private readonly Timer _pollTimer;
    private readonly Timer _stateTimer;
    private readonly Timer _captureTimer;
    private readonly Timer _closeTimer;

    private SerialPort _serial;
    private readonly SerialPortStatus _serialStatus = new SerialPortStatus();
    private string _rxBuffer = string.Empty;
    private bool _isPaused;
    private bool _closedByUser;
    private bool _statusInFlight;
    private DateTime _startUtc;
    private DateTime _lastStatusSendUtc;
    private DateTime _lastValidStatusUtc;
    private DateTime _lastSampleUtc;
    private DateTime _lastStateWriteUtc;
    private long _sampleCount;
    private long _validSampleCount;
    private long _rxLineCount;
    private long _txLineCount;
    private long _parseErrorCount;
    private bool _pauseBreakPending;
    private double _lastRelativeDegrees;
    private double _lastAngle;
    private long _lastHealth;
    private long _lastDiag;
    private long _lastRawEncoder;
    private string _lastError = string.Empty;

    private readonly AngleTracker _angleTracker = new AngleTracker();
    private readonly object _ioLock = new object();
    private StreamWriter _statusWriter;
    private StreamWriter _rawWriter;
    private readonly string _statusPath;
    private readonly string _rawPath;
    private readonly string _statePath;
    private readonly string _processId;

    public MainForm(ViewerOptions options)
    {
      _portName = options.PortName;
      _outputDirectory = options.OutputDirectory;
      _capturePath = options.CapturePath;
      _closeAfterSeconds = options.CloseAfterSeconds;
      _processId = Process.GetCurrentProcess().Id.ToString(CultureInfo.InvariantCulture);

      Directory.CreateDirectory(_outputDirectory);
      string ts = DateTime.UtcNow.ToString("yyyyMMdd_HHmmss_fff", CultureInfo.InvariantCulture);
      string pidTag = _processId;
      _statusPath = Path.Combine(_outputDirectory, "encoder-status-" + ts + "-" + pidTag + ".csv");
      _rawPath = Path.Combine(_outputDirectory, "encoder-serial-" + ts + "-" + pidTag + ".log");
      _statePath = Path.Combine(_outputDirectory, "latest-state.json");

      _statusWriter = new StreamWriter(_statusPath, false, System.Text.Encoding.UTF8);
      _rawWriter = new StreamWriter(_rawPath, false, System.Text.Encoding.UTF8);
      _statusWriter.WriteLine("utc,host_elapsed_s,enc,angle_deg,relative_deg,valid,diag,health,age_us,enc_ok,enc_err,uart_err,adc_bad,deadline,mode,moe,off,seq");

      Text = "GL30 编码器曲线查看器（仅STATUS）";
      MinimumSize = new Size(1100, 780);
      Size = new Size(1320, 880);
      StartPosition = FormStartPosition.CenterScreen;
      FormBorderStyle = FormBorderStyle.Sizable;

      _lblTitle = MakeLabel("GL30 手转编码器  |  仅读取STATUS · 不启用电机 · B1松开", new Font("Microsoft YaHei", 13f, FontStyle.Bold), 42);
      _lblTitle.Dock = DockStyle.Top;
      _lblTitle.TextAlign = ContentAlignment.MiddleLeft;

      var split = new SplitContainer
      {
        Dock = DockStyle.Fill,
        Size = new Size(1300, 790),
        Orientation = Orientation.Vertical,
        SplitterWidth = 6,
        SplitterDistance = 405,
        FixedPanel = FixedPanel.Panel1
      };

      var left = new Panel { Dock = DockStyle.Fill };
      var statusPanel = new FlowLayoutPanel
      {
        Dock = DockStyle.Fill,
        FlowDirection = FlowDirection.TopDown,
        WrapContents = false,
        AutoScroll = true
      };

      _lblConnection = MakeLabel("连接: 未连接");
      _lblWarning = MakeLabel("窗口只读；输出关闭不代表外部动力已经断电。", new Font("Microsoft YaHei", 11f, FontStyle.Bold), 42);
      _lblWarning.ForeColor = Color.DarkRed;
      _lblWarning.BackColor = Color.LemonChiffon;

      _btnConnect = new Button { Text = "连接", Width = 120, Height = 32 };
      _btnConnect.Click += (s, e) => AutoConnect();
      _btnDisconnect = new Button { Text = "断开", Width = 120, Height = 32 };
      _btnDisconnect.Click += (s, e) => Disconnect("用户手动断开");
      _chkPause = new CheckBox { Text = "暂停显示（仍采集）", Width = 170, Height = 28 };
      _chkPause.CheckedChanged += (s, e) =>
      {
        bool wasPaused = _isPaused;
        _isPaused = _chkPause.Checked;
        if (wasPaused && !_isPaused && _pauseBreakPending)
        {
          ClearSeries();
          _pauseBreakPending = false;
        }
        if (_isPaused) _pauseBreakPending = true;
      };
      _btnOpenDir = new Button { Text = "打开记录目录", Width = 140, Height = 32 };
      _btnOpenDir.Click += (s, e) =>
      {
        try { Process.Start(_outputDirectory); }
        catch (Exception ex) { _lblParseError.Text = "打开目录失败: " + ex.Message; }
      };
      _btnResetRelative = new Button { Text = "本地相对清零", Width = 120, Height = 32 };
      _btnResetRelative.Click += (s, e) => ResetRelative();

      var actionRow = new FlowLayoutPanel { Width = 370, MaximumSize = new Size(370, 0), AutoSize = true, FlowDirection = FlowDirection.LeftToRight, WrapContents = true };
      actionRow.Controls.Add(_btnConnect);
      actionRow.Controls.Add(_btnDisconnect);
      actionRow.Controls.Add(_chkPause);
      actionRow.Controls.Add(_btnOpenDir);
      actionRow.Controls.Add(_btnResetRelative);

      _lblMode = MakeLabel("mode=--");
      _lblFault = MakeLabel("fault=--");
      _lblHealth = MakeLabel("health=--");
      _lblAge = MakeLabel("age_us=--");
      _lblMoe = MakeLabel("moe=--");
      _lblDrv = MakeLabel("drv=--");
      _lblOff = MakeLabel("off=--");
      _lblDiag = MakeLabel("diag=--", new Font("Microsoft YaHei", 10f), 48);
      _lblTiming = MakeLabel("自检/时序: --", null, 48);
      _lblCurrentAngle = MakeLabel("当前角度: --°", new Font("Microsoft YaHei", 20f, FontStyle.Bold), 56);
      _lblRawEncoder = MakeLabel("原始码: --", new Font("Microsoft YaHei", 12f), 34);
      _lblRelativeAngle = MakeLabel("相对角: --°", new Font("Microsoft YaHei", 14f), 38);
      _lblCounters = MakeLabel("读取/错误计数: --", new Font("Microsoft YaHei", 10f), 68);
      _lblStatus = MakeLabel("状态: --", new Font("Microsoft YaHei", 10f), 28);
      _lblParseError = MakeLabel("最新解析: 无", null, 42);
      _lblReason = MakeLabel("备注: --", new Font("Microsoft YaHei", 9f), 54);

      statusPanel.Controls.Add(_lblConnection);
      statusPanel.Controls.Add(_lblWarning);
      statusPanel.Controls.Add(_lblCurrentAngle);
      statusPanel.Controls.Add(_lblRawEncoder);
      statusPanel.Controls.Add(_lblRelativeAngle);
      statusPanel.Controls.Add(actionRow);
      statusPanel.Controls.Add(_lblMode);
      statusPanel.Controls.Add(_lblFault);
      statusPanel.Controls.Add(_lblHealth);
      statusPanel.Controls.Add(_lblAge);
      statusPanel.Controls.Add(_lblMoe);
      statusPanel.Controls.Add(_lblDrv);
      statusPanel.Controls.Add(_lblOff);
      statusPanel.Controls.Add(_lblDiag);
      statusPanel.Controls.Add(_lblTiming);
      statusPanel.Controls.Add(_lblCounters);
      statusPanel.Controls.Add(_lblStatus);
      statusPanel.Controls.Add(_lblParseError);
      statusPanel.Controls.Add(_lblReason);

      var warningTip = MakeLabel("注意：红色警告为只读监测，不代表断开输出或替代机械制动。",
        new Font("Microsoft YaHei", 9f, FontStyle.Bold), 42);
      warningTip.ForeColor = Color.DarkRed;
      statusPanel.Controls.Add(warningTip);

      left.Controls.Add(statusPanel);
      split.Panel1.Controls.Add(left);

      var charts = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2 };
      charts.RowStyles.Add(new RowStyle(SizeType.Percent, 50f));
      charts.RowStyles.Add(new RowStyle(SizeType.Percent, 50f));

      _chartAngle = BuildChart("单圈角（0~360°）", "angle_deg", "时间(s)");
      _chartRelative = BuildChart("相对连续转角（最近30秒）", "relative_deg", "时间(s)");
      _seriesAngle = _chartAngle.Series["Angle"];
      _seriesRelative = _chartRelative.Series["Angle"];

      charts.Controls.Add(_chartAngle, 0, 0);
      charts.Controls.Add(_chartRelative, 0, 1);
      split.Panel2.Controls.Add(charts);

      Controls.Add(split);
      Controls.Add(_lblTitle);

      _pollTimer = new Timer();
      _pollTimer.Interval = PollIntervalMs;
      _pollTimer.Tick += PollTimerTick;

      _stateTimer = new Timer();
      _stateTimer.Interval = 1000;
      _stateTimer.Tick += (s, e) =>
      {
        WriteStateJson();
        FlushLogs();
      };

      _captureTimer = new Timer();
      _captureTimer.Interval = 2000;
      _captureTimer.Tick += (s, e) =>
      {
        _captureTimer.Stop();
        DoCapture();
        if (_closeAfterSeconds.HasValue)
        {
          StartCloseTimer();
        }
      };

      if (_closeAfterSeconds.HasValue && _closeAfterSeconds.Value > 0)
      {
        _closeTimer = new Timer();
        _closeTimer.Interval = Math.Max(500, _closeAfterSeconds.Value * 1000);
        _closeTimer.Tick += (s, e) =>
        {
          _closeTimer.Stop();
          Close();
        };
      }
      else
      {
        _closeTimer = null;
      }

      Shown += (s, e) =>
      {
        AutoConnect();
        if (!string.IsNullOrWhiteSpace(_capturePath))
        {
          _captureTimer.Start();
        }
        else if (_closeAfterSeconds.HasValue && _closeAfterSeconds.Value > 0)
        {
          StartCloseTimer();
        }
      };
      FormClosed += (s, e) => { _closedByUser = true; _serialStatus.IsStopping = true; Cleanup(); };

      _pollTimer.Start();
      _stateTimer.Start();
      UpdateConnectionUi();
    }

    private static Label MakeLabel(string text, Font font = null, int height = 28)
    {
      return new Label
      {
        Text = text,
        AutoSize = false,
        Width = 370,
        Height = height,
        Font = font ?? new Font("Microsoft YaHei", 10f),
        TextAlign = ContentAlignment.MiddleLeft
      };
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
      base.OnFormClosing(e);
      Cleanup();
    }

    private Chart BuildChart(string title, string yTitle, string xTitle)
    {
      var chart = new Chart { Dock = DockStyle.Fill, BorderlineColor = Color.DarkGray, BorderlineWidth = 1, BorderSkin = { SkinStyle = BorderSkinStyle.Emboss } };
      var area = new ChartArea { Name = "main" };
      area.AxisX.Title = xTitle;
      area.AxisY.Title = yTitle;
      area.AxisX.LabelStyle.Format = "F1";
      area.AxisY.LabelStyle.Format = "F1";
      area.AxisX.MajorGrid.LineColor = Color.Gainsboro;
      area.AxisY.MajorGrid.LineColor = Color.Gainsboro;
      if (yTitle.IndexOf("angle_deg", StringComparison.OrdinalIgnoreCase) >= 0)
      {
        area.AxisY.Minimum = 0;
        area.AxisY.Maximum = 360;
      }
      chart.ChartAreas.Add(area);
      chart.Titles.Add(title);
      var series = new Series("Angle")
      {
        ChartType = SeriesChartType.FastLine,
        Color = Color.SteelBlue,
        IsVisibleInLegend = false,
        XValueType = ChartValueType.Double,
        YValueType = ChartValueType.Double,
        BorderWidth = 2,
        ChartArea = "main"
      };
      chart.Series.Add(series);
      return chart;
    }

    private void StartCloseTimer()
    {
      if (_closeTimer != null && !_closeTimer.Enabled)
      {
        _closeTimer.Start();
      }
    }

    private void AutoConnect()
    {
      if (_serialStatus.IsStopping || _serialStatus.IsConnected) return;
      Connect(_portName);
    }

    private void Connect(string portName)
    {
      if (_serialStatus.IsStopping) return;

      try
      {
        if (_serial != null)
        {
          try
          {
            _serial.Close();
          }
          catch
          {
          }
          _serial.Dispose();
          _serial = null;
        }

        _serial = new SerialPort(portName, 115200, Parity.None, 8, StopBits.One)
        {
          DtrEnable = false,
          RtsEnable = false,
          ReadTimeout = -1,
          WriteTimeout = 500
        };
        _serial.Open();
        _serial.DiscardInBuffer();
        _serial.DiscardOutBuffer();
        _rxBuffer = string.Empty;
        ClearSeries();
        _serialStatus.IsConnected = true;
        _serialStatus.Port = portName;
        _serialStatus.ConnectTimeUtc = DateTime.UtcNow;
        _serialStatus.HealthReason = "已连接";
        _startUtc = DateTime.UtcNow;
        _lastStatusSendUtc = DateTime.MinValue;
        _lastValidStatusUtc = DateTime.MinValue;
        _lastSampleUtc = DateTime.MinValue;
        _statusInFlight = false;
        _pauseBreakPending = false;
        _parseErrorCount = 0;
        _sampleCount = 0;
        _validSampleCount = 0;
        _rxLineCount = 0;
        _txLineCount = 0;
        _lastError = "已连接";
        _angleTracker.Reset();
        _lblParseError.Text = "解析: 已建立串口";
      }
      catch (Exception ex)
      {
        _serialStatus.HealthReason = "连接失败: " + ex.Message;
        _serialStatus.IsConnected = false;
      }
      finally
      {
        UpdateConnectionUi();
      }
    }

    private void Disconnect(string reason)
    {
      if (_serialStatus.IsStopping) return;
      if (_serial != null)
      {
        try
        {
          _serial.Close();
        }
        catch
        {
        }
        try
        {
          _serial.Dispose();
        }
        catch
        {
        }
        _serial = null;
      }

      _serialStatus.IsConnected = false;
      _serialStatus.HealthReason = reason;
      _statusInFlight = false;
      _pauseBreakPending = false;
      _angleTracker.Reset();
      _lastSampleUtc = DateTime.MinValue;
      ClearSeries();
      AddSeriesGap(DateTime.UtcNow);
      _lblParseError.Text = "状态: " + reason;
      _lastError = reason;
      UpdateConnectionUi();
    }

    private void PollTimerTick(object sender, EventArgs e)
    {
      if (_closedByUser || _serialStatus.IsStopping) return;
      if (!_serialStatus.IsConnected) return;

      DateTime now = DateTime.UtcNow;

      if (_serial != null && _serial.IsOpen)
      {
        try
        {
          string chunk = _serial.ReadExisting();
          if (!string.IsNullOrEmpty(chunk))
          {
            AppendRaw(now, "RX", chunk);
            ConsumeIncoming(chunk);
          }
        }
        catch (TimeoutException)
        {
        }
        catch (Exception ex)
        {
          _serialStatus.HealthReason = "读串口失败: " + ex.Message;
          _parseErrorCount++;
          Disconnect("读串口失败");
          return;
        }
      }

      now = DateTime.UtcNow;
      DateTime timeoutBase = (_lastValidStatusUtc != DateTime.MinValue) ? _lastValidStatusUtc : _serialStatus.ConnectTimeUtc;
      if (timeoutBase != DateTime.MinValue && (now - timeoutBase).TotalMilliseconds > StatusTimeoutMs)
      {
        Disconnect("2秒未收到完整有效STATUS");
        return;
      }

      double stale = (now - timeoutBase).TotalSeconds;
      if (stale > StaleWarnSeconds)
      {
        _lblStatus.ForeColor = Color.DarkRed;
      }
      else
      {
        _lblStatus.ForeColor = Color.Teal;
      }
      _lblStatus.Text = "距上次回报: " + stale.ToString("F1", CultureInfo.InvariantCulture) + " s";

      if (_serialStatus.IsConnected && !_statusInFlight && (now - _lastStatusSendUtc).TotalMilliseconds >= StatusIntervalMs)
      {
        SendStatus();
        _lastStatusSendUtc = now;
      }
    }

    private void SendStatus()
    {
      if (!_serialStatus.IsConnected || _serial == null || !_serial.IsOpen || _serialStatus.IsStopping) return;
      try
      {
        _serial.WriteLine("STATUS");
        _txLineCount++;
        _statusInFlight = true;
        AppendRaw(DateTime.UtcNow, "TX", "STATUS");
      }
      catch (Exception ex)
      {
        _serialStatus.HealthReason = "发送失败: " + ex.Message;
        Disconnect("发送失败");
      }
    }

    private void ConsumeIncoming(string text)
    {
      if (string.IsNullOrEmpty(text)) return;
      _rxBuffer += text;

      int idx;
      while ((idx = _rxBuffer.IndexOf('\n')) >= 0)
      {
        string line = _rxBuffer.Substring(0, idx);
        _rxBuffer = _rxBuffer.Substring(idx + 1);
        ProcessLine(line.Trim('\r'));
      }

      if (_rxBuffer.Length > MaxRxLineBuffer)
      {
        _rxBuffer = string.Empty;
        _parseErrorCount++;
        _lblParseError.Text = "RX缓存过大，已重置，可能存在数据不同步";
      }
    }

    private void ProcessLine(string line)
    {
      _rxLineCount++;
      if (string.IsNullOrWhiteSpace(line)) return;

      DateTime now = DateTime.UtcNow;
      if (!line.StartsWith("STATUS", StringComparison.Ordinal))
      {
        if (line.StartsWith("OK", StringComparison.Ordinal) ||
            line.StartsWith("PING", StringComparison.Ordinal) ||
            line.StartsWith("SELFTEST", StringComparison.Ordinal))
        {
          return;
        }

        _statusInFlight = false;
        _parseErrorCount++;
        if (line.StartsWith("BOOT", StringComparison.Ordinal) ||
            line.StartsWith("ERR", StringComparison.Ordinal) ||
            line.StartsWith("EVENT", StringComparison.Ordinal))
        {
          _serialStatus.HealthReason = "非STATUS状态: " + line;
          AddSeriesGap(now);
          _angleTracker.Reset();
          _lastSampleUtc = DateTime.MinValue;
          _lastRawEncoder = 0;
          _lblParseError.Text = "非STATUS: " + line;
          return;
        }

        _serialStatus.HealthReason = "解析失败: 非STATUS => " + line;
        _lblParseError.Text = "解析原因: 非STATUS => " + line;
        AddSeriesGap(now);
        _angleTracker.Reset();
        _lastSampleUtc = DateTime.MinValue;
        return;
      }

      StatusSample sample;
      string error;
      if (!StatusSample.TryParse(line, out sample, out error))
      {
        _statusInFlight = false;
        _parseErrorCount++;
        _serialStatus.HealthReason = "STATUS解析失败: " + error;
        _lblParseError.Text = "解析失败: " + error;
        AddSeriesGap(now);
        _angleTracker.Reset();
        _lastSampleUtc = DateTime.MinValue;
        return;
      }

      _statusInFlight = false;
      _lastValidStatusUtc = now;
      _sampleCount++;
      _serialStatus.HealthReason = string.Empty;
      RecordLatest(sample);

      long encOk = sample.Fields["enc_ok"];
      double elapsed = (now - _startUtc).TotalSeconds;
      double dt = _lastSampleUtc == DateTime.MinValue ? 0.0 : (now - _lastSampleUtc).TotalSeconds;
      bool statusGood = IsStatusUsableForChart(sample);
      bool continuity = false;

      try
      {
        if (statusGood)
        {
          continuity = _angleTracker.Update(sample.RawEncoder, dt, encOk);
        }
      }
      catch
      {
        _angleTracker.Reset();
        continuity = false;
      }

      bool needGap = (!statusGood || !continuity);
      if (needGap)
      {
        AddSeriesGap(now);
      }
      if (!statusGood)
      {
        _angleTracker.Reset();
        _lastSampleUtc = DateTime.MinValue;
      }

      WriteStatusCsv(sample, now, statusGood);
      UpdateStatusText(sample);
      _lastRawEncoder = sample.RawEncoder;
      _lastDiag = sample.Diagnostic;
      _lastHealth = sample.Fields.ContainsKey("health") ? sample.Fields["health"] : 0;
      _lastAngle = sample.AngleDegrees;
      if (statusGood)
      {
        _lastRelativeDegrees = _angleTracker.RelativeDegrees;
        _validSampleCount++;
      }

      _lblParseError.Text = statusGood ? "解析成功" : "解析成功但健康检查异常";
      if (!statusGood)
      {
        _parseErrorCount++;
        _lblRelativeAngle.Text = "相对角: --°";
      }
      else if (_isPaused)
      {
        _lblRelativeAngle.Text = "相对角: --°";
      }
      else
      {
        AppendSeriesPoint(_seriesAngle, elapsed, sample.AngleDegrees, false);
        AppendSeriesPoint(_seriesRelative, elapsed, _angleTracker.RelativeDegrees, false);
        _lblRelativeAngle.Text = "相对角: " + _angleTracker.RelativeDegrees.ToString("F2", CultureInfo.InvariantCulture) + "°";
      }

      _lastSampleUtc = statusGood ? now : DateTime.MinValue;
      if (!_isPaused)
      {
        TrimSeries(_seriesAngle, elapsed);
        TrimSeries(_seriesRelative, elapsed);
        RefreshChartAxes(_seriesAngle, _chartAngle, elapsed);
        RefreshChartAxes(_seriesRelative, _chartRelative, elapsed);
      }
    }

    private bool IsStatusUsableForChart(StatusSample sample)
    {
      long age = sample.Fields.ContainsKey("age_us") ? sample.Fields["age_us"] : long.MaxValue;
      long health = sample.Fields.ContainsKey("health") ? sample.Fields["health"] : 0;
      bool ageOk = age <= 750;
      bool healthOk = (health & 0x2L) != 0;
      if (!sample.EncoderValid) return false;
      return ageOk && healthOk;
    }

    private void UpdateStatusText(StatusSample sample)
    {
      long mode = sample.Fields.ContainsKey("mode") ? sample.Fields["mode"] : 0;
      long fault = sample.Fields.ContainsKey("fault") ? sample.Fields["fault"] : 0;
      long health = sample.Fields.ContainsKey("health") ? sample.Fields["health"] : 0;
      long age = sample.Fields.ContainsKey("age_us") ? sample.Fields["age_us"] : 0;
      long moe = sample.Fields.ContainsKey("moe") ? sample.Fields["moe"] : 0;
      long drv = sample.Fields.ContainsKey("drv") ? sample.Fields["drv"] : 0;
      long calibrated = sample.Fields.ContainsKey("calibrated") ? sample.Fields["calibrated"] : 0;
      long button = sample.Fields.ContainsKey("button") ? sample.Fields["button"] : 0;
      long encOk = sample.Fields.ContainsKey("enc_ok") ? sample.Fields["enc_ok"] : 0;
      long encErr = sample.Fields.ContainsKey("enc_err") ? sample.Fields["enc_err"] : 0;
      long uartErr = sample.Fields.ContainsKey("uart_err") ? sample.Fields["uart_err"] : 0;
      long adcBad = sample.Fields.ContainsKey("adc_bad") ? sample.Fields["adc_bad"] : 0;
      long deadline = sample.Fields.ContainsKey("deadline") ? sample.Fields["deadline"] : 0;
      long selfLeft = sample.Fields.ContainsKey("self_left") ? sample.Fields["self_left"] : 0;
      long selfFail = sample.Fields.ContainsKey("self_fail") ? sample.Fields["self_fail"] : 0;
      long selfMax = sample.Fields.ContainsKey("self_max") ? sample.Fields["self_max"] : 0;
      long isrMax = sample.Fields.ContainsKey("isr_max") ? sample.Fields["isr_max"] : 0;

      _lblConnection.Text = "连接: " + (_serialStatus.IsConnected ? "已连接 " + _portName : "未连接");
      _lblMode.Text = "mode=" + mode;
      _lblFault.Text = "fault=" + fault;
      _lblHealth.Text = "health=" + health;
      _lblAge.Text = "age_us=" + age;
      _lblMoe.Text = "moe=" + moe;
      _lblDrv.Text = "drv=" + drv;
      long off = sample.Fields.ContainsKey("off") ? sample.Fields["off"] : 0;
      _lblOff.Text = "off=" + off + (sample.OutputOff ? "（固件输出关断）" : "（输出异常）");
      _lblWarning.Text = sample.OutputOff ? "固件输出关断（仅读）" : "输出异常";
      _lblWarning.BackColor = sample.OutputOff ? Color.LightGreen : Color.MistyRose;
      _lblWarning.ForeColor = sample.OutputOff ? Color.DarkGreen : Color.DarkRed;
      _lblCurrentAngle.Text = sample.EncoderValid ? "当前角度: " + sample.AngleDegrees.ToString("F2", CultureInfo.InvariantCulture) + "°" : "当前角度: 无效";
      _lblRawEncoder.Text = "原始码: " + sample.RawEncoder;
      _lblDiag.Text = FormatDiag(sample.Diagnostic);
      _lblCounters.Text = "读取成功 " + encOk + "    读取错误 " + encErr + "\r\nUART错误 " + uartErr + "    ADC异常 " + adcBad + "\r\n期限超限 " + deadline;
      _lblCounters.ForeColor = (encErr != 0 || uartErr != 0 || adcBad != 0 || deadline != 0) ? Color.DarkRed : Color.Black;
      _lblTiming.Text = "自检剩余 " + selfLeft + " / 失败 " + selfFail + "\r\n自检最大 " + selfMax + " / IRQ最大 " + isrMax + " cycles";
      _lblReason.Text = "diag=" + sample.Diagnostic + " (OCF=" + ((sample.Diagnostic & 0x100L) != 0 ? "1" : "0") + ",COF=" + ((sample.Diagnostic & 0x200L) != 0 ? "1" : "0") + ",COMP=" + ((sample.Diagnostic & 0x400L) != 0 ? "1" : "0") + ")";
      _lblStatus.ForeColor = (sample.OutputOff ? Color.DarkRed : Color.Black);

      string healthErr = string.Empty;
      if (mode != 0) healthErr = "mode!=" + mode + "(非0)";
      if (fault != 0) healthErr = healthErr + (string.IsNullOrEmpty(healthErr) ? "" : "; ") + "fault=" + fault + "(异常)";
      if (moe != 0) healthErr = healthErr + (string.IsNullOrEmpty(healthErr) ? "" : "; ") + "moe=" + moe + "(非0)";
      if (!sample.EncoderValid) healthErr += " 编码器无效：请检查诊断位/新鲜度";
      if (calibrated != 0) healthErr = healthErr + (string.IsNullOrEmpty(healthErr) ? "" : "; ") + "calibrated=" + calibrated + "(未归零)";
      if (button != 0) healthErr = healthErr + (string.IsNullOrEmpty(healthErr) ? "" : "; ") + "button=" + button + "(B1未松开)";
      long statusAge = age;
      if (!((health & 0x2L) != 0)) healthErr = healthErr + (string.IsNullOrEmpty(healthErr) ? "" : "; ") + "health未置位bit1";
      if (statusAge > 750) healthErr = healthErr + (string.IsNullOrEmpty(healthErr) ? "" : "; ") + "age_us超过750";
      if (encErr != 0 || uartErr != 0 || adcBad != 0 || deadline != 0) healthErr += " 错误计数非零，请检查";
      _lblReason.Text = "备注: " + (string.IsNullOrEmpty(healthErr) ? "缓慢手转；显示约10Hz，不是完整4kHz流" : healthErr);
      if (!string.IsNullOrEmpty(healthErr))
      {
        _lblWarning.Text = "安全监测:" + healthErr;
        _lblWarning.ForeColor = Color.DarkRed;
        _lblWarning.BackColor = Color.MistyRose;
      }
      else
      {
        _lblWarning.Text = sample.OutputOff ? "固件输出关断（仅读）" : "输出异常";
        _lblWarning.ForeColor = sample.OutputOff ? Color.DarkGreen : Color.DarkRed;
        _lblWarning.BackColor = sample.OutputOff ? Color.LightGreen : Color.MistyRose;
      }
    }

    private static string FormatDiag(long diag)
    {
      string ocf = (diag & 0x100L) != 0 ? "OCF=1" : "OCF=0";
      string cof = (diag & 0x200L) != 0 ? "COF=1" : "COF=0";
      string compLow = (diag & 0x400L) != 0 ? "COMP_low=1" : "COMP_low=0";
      string compHigh = (diag & 0x800L) != 0 ? "COMP_high=1" : "COMP_high=0";
      return "diag=0x" + diag.ToString("X4", CultureInfo.InvariantCulture) + "  " + ocf + "  " + cof + "\r\n" + compLow + "  " + compHigh;
    }

    private void AppendSeriesPoint(Series series, double elapsed, double y, bool breakBefore)
    {
      if (_isPaused) return;

      if (breakBefore)
      {
        DataPoint gap = new DataPoint();
        gap.XValue = elapsed;
        gap.IsEmpty = true;
        series.Points.Add(gap);
      }

      DataPoint point = new DataPoint();
      point.XValue = elapsed;
      point.YValues = new[] { y };
      series.Points.Add(point);
    }

    private void AddSeriesGap(DateTime now)
    {
      double elapsed = (now - _startUtc).TotalSeconds;
      if (_isPaused)
      {
        _pauseBreakPending = true;
        return;
      }
      DataPoint gap1 = new DataPoint();
      gap1.XValue = elapsed;
      gap1.IsEmpty = true;
      _seriesAngle.Points.Add(gap1);

      DataPoint gap2 = new DataPoint();
      gap2.XValue = elapsed;
      gap2.IsEmpty = true;
      _seriesRelative.Points.Add(gap2);
    }

    private void ClearSeries()
    {
      _seriesAngle.Points.Clear();
      _seriesRelative.Points.Clear();
      _pauseBreakPending = true;
      if (!_isPaused)
      {
        _pauseBreakPending = false;
      }
    }

    private void ResetRelative()
    {
      _angleTracker.Reset();
      _lblRelativeAngle.Text = "相对角: --°";
      _lastRelativeDegrees = 0.0;
    }

    private void RecordLatest(StatusSample sample)
    {
      _lastAngle = sample.AngleDegrees;
      _lastRawEncoder = sample.RawEncoder;
      _lastDiag = sample.Diagnostic;
      _lastHealth = sample.Fields.ContainsKey("health") ? sample.Fields["health"] : 0;
      _lastRelativeDegrees = _angleTracker.RelativeDegrees;
      _lastError = _serialStatus.HealthReason;
      _lastError = _lastError ?? string.Empty;
    }

    private void TrimSeries(Series series, double now)
    {
      while (series.Points.Count > MaxChartPoints)
      {
        series.Points.RemoveAt(0);
      }

      while (series.Points.Count > 0)
      {
        if (now - series.Points[0].XValue > RollingWindowSeconds)
        {
          series.Points.RemoveAt(0);
        }
        else
        {
          break;
        }
      }
    }

    private void RefreshChartAxes(Series series, Chart chart, double elapsed)
    {
      if (series.Points.Count == 0) return;

      double minX = elapsed - RollingWindowSeconds;
      if (minX < 0.0) minX = 0.0;
      chart.ChartAreas["main"].AxisX.Minimum = minX;
      chart.ChartAreas["main"].AxisX.Maximum = elapsed + 0.2;

      if (series.Points.Count > 0)
      {
        if (series == _seriesRelative)
        {
          double minY = double.MaxValue;
          double maxY = double.MinValue;
          foreach (DataPoint p in series.Points)
          {
            if (p.IsEmpty) continue;
            if (p.YValues.Length > 0)
            {
              double v = p.YValues[0];
              if (v < minY) minY = v;
              if (v > maxY) maxY = v;
            }
          }
          if (minY != double.MaxValue && maxY != double.MinValue)
          {
            double pad = Math.Max(30.0, (maxY - minY) * 0.15);
            chart.ChartAreas["main"].AxisY.Minimum = minY - pad;
            chart.ChartAreas["main"].AxisY.Maximum = maxY + pad;
          }
        }
      }
    }

    private void WriteStatusCsv(StatusSample sample, DateTime utcNow, bool valid)
    {
      if (_statusWriter == null) return;
      long seq = _sampleCount;
      string line = string.Format(CultureInfo.InvariantCulture,
        "{0:O},{1:F3},{2},{3:F3},{4:F3},{5},{6},{7},{8},{9},{10},{11},{12},{13},{14},{15},{16},{17}",
        utcNow,
        (utcNow - _startUtc).TotalSeconds,
        sample.RawEncoder,
        sample.AngleDegrees,
        _angleTracker.RelativeDegrees,
        valid ? 1 : 0,
        sample.Diagnostic,
        sample.Fields.ContainsKey("health") ? sample.Fields["health"] : 0,
        sample.Fields.ContainsKey("age_us") ? sample.Fields["age_us"] : 0,
        sample.Fields.ContainsKey("enc_ok") ? sample.Fields["enc_ok"] : 0,
        sample.Fields.ContainsKey("enc_err") ? sample.Fields["enc_err"] : 0,
        sample.Fields.ContainsKey("uart_err") ? sample.Fields["uart_err"] : 0,
        sample.Fields.ContainsKey("adc_bad") ? sample.Fields["adc_bad"] : 0,
        sample.Fields.ContainsKey("deadline") ? sample.Fields["deadline"] : 0,
        sample.Fields.ContainsKey("mode") ? sample.Fields["mode"] : 0,
        sample.Fields.ContainsKey("moe") ? sample.Fields["moe"] : 0,
        sample.Fields.ContainsKey("off") ? sample.Fields["off"] : 0,
        seq);
      _statusWriter.WriteLine(line);
    }

    private void AppendRaw(DateTime utcNow, string direction, string data)
    {
      if (_rawWriter == null) return;
      string escaped = data.Replace("\"", "\"\"");
      _rawWriter.WriteLine(string.Format(CultureInfo.InvariantCulture, "{0:O},{1},{2},\"{3}\"",
        utcNow,
        "serial",
        direction,
        escaped));
    }

    private void WriteStateJson()
    {
      if ((DateTime.UtcNow - _lastStateWriteUtc).TotalSeconds < 1.0) return;

      lock (_ioLock)
      {
        using (var sw = new StreamWriter(_statePath, false, System.Text.Encoding.UTF8))
        {
          sw.WriteLine("{");
          sw.WriteLine("  \"processId\": \"" + _processId + "\",");
          sw.WriteLine("  \"connected\": " + (_serialStatus.IsConnected ? "true" : "false") + ",");
          sw.WriteLine("  \"port\": \"" + _portName.Replace("\"", "\\\"") + "\",");
          sw.WriteLine("  \"outputDirectory\": \"" + _outputDirectory.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\",");
          sw.WriteLine("  \"sampleCount\": " + _sampleCount + ",");
          sw.WriteLine("  \"validSampleCount\": " + _validSampleCount + ",");
          sw.WriteLine("  \"statusLines\": " + _rxLineCount + ",");
          sw.WriteLine("  \"txLines\": " + _txLineCount + ",");
          sw.WriteLine("  \"errorCount\": " + _parseErrorCount + ",");
           sw.WriteLine("  \"lastError\": \"" + _lastError.Replace("\"", "\\\"") + "\",");
          sw.WriteLine("  \"lastSampleUtc\": \"" + (_lastValidStatusUtc == DateTime.MinValue ? string.Empty : _lastValidStatusUtc.ToString("O")) + "\",");
          sw.WriteLine("  \"angle\": " + _lastAngle.ToString(CultureInfo.InvariantCulture) + ",");
          sw.WriteLine("  \"rawEncoder\": " + _lastRawEncoder + ",");
          sw.WriteLine("  \"relativeAngle\": " + _lastRelativeDegrees.ToString(CultureInfo.InvariantCulture) + ",");
          sw.WriteLine("  \"health\": " + _lastHealth + ",");
          sw.WriteLine("  \"diag\": " + _lastDiag + ",");
          sw.WriteLine("  \"lastStatusUtc\": \"" + (_lastValidStatusUtc == DateTime.MinValue ? string.Empty : _lastValidStatusUtc.ToString("O")) + "\",");
          sw.WriteLine("  \"statusFile\": \"" + _statusPath.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\",");
          sw.WriteLine("  \"serialLog\": \"" + _rawPath.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"");
          sw.WriteLine("}");
        }
      }
      _lastStateWriteUtc = DateTime.UtcNow;
    }

    private void FlushLogs()
    {
      lock (_ioLock)
      {
        if (_statusWriter != null) _statusWriter.Flush();
        if (_rawWriter != null) _rawWriter.Flush();
      }
    }

    private void DoCapture()
    {
      if (_serialStatus.IsStopping || string.IsNullOrWhiteSpace(_capturePath)) return;
      try
      {
        using (Bitmap bmp = new Bitmap(Width, Height))
        {
          DrawToBitmap(bmp, new Rectangle(0, 0, Width, Height));
          bmp.Save(_capturePath);
          AppendRaw(DateTime.UtcNow, "LOG", "CAPTURE:" + _capturePath);
        }
      }
      catch (Exception ex)
      {
        _lblParseError.Text = "截图失败: " + ex.Message;
      }
    }

    private void UpdateConnectionUi()
    {
      if (this.InvokeRequired)
      {
        BeginInvoke(new Action(UpdateConnectionUi));
        return;
      }
      _btnConnect.Enabled = !_serialStatus.IsConnected;
      _btnDisconnect.Enabled = _serialStatus.IsConnected;
      if (!_serialStatus.IsConnected)
      {
        _lblConnection.Text = "连接: 未连接";
      }
      else
      {
        _lblConnection.Text = "连接: 已连接 " + _portName;
      }
    }

    private void Cleanup()
    {
      _serialStatus.IsStopping = true;
      try { _pollTimer.Stop(); } catch { }
      try { _stateTimer.Stop(); } catch { }
      try { _captureTimer.Stop(); } catch { }
      if (_closeTimer != null) try { _closeTimer.Stop(); } catch { }

      if (_serial != null)
      {
        try
        {
          _serial.Close();
          _serial.Dispose();
        }
        catch { }
        _serial = null;
      }
      _serialStatus.IsConnected = false;

      _lastStateWriteUtc = DateTime.MinValue;
      WriteStateJson();
      FlushLogs();
      lock (_ioLock)
      {
        if (_statusWriter != null)
        {
          try
          {
            _statusWriter.Close();
          }
          catch { }
          _statusWriter = null;
        }

        if (_rawWriter != null)
        {
          try
          {
            _rawWriter.Close();
          }
          catch { }
          _rawWriter = null;
        }
      }
    }
  }

  internal static class Program
  {
    [STAThread]
    public static void Main(string[] args)
    {
      string port = "COM4";
      string output = AppDomain.CurrentDomain.BaseDirectory;
      string capture = string.Empty;
      int? closeAfter = null;

      for (int i = 0; i < args.Length; ++i)
      {
        if (string.Equals(args[i], "--port", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
        {
          port = args[++i];
        }
        else if (string.Equals(args[i], "--output", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
        {
          output = args[++i];
        }
        else if (string.Equals(args[i], "--capture", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
        {
          capture = args[++i];
        }
        else if (string.Equals(args[i], "--close-after", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length)
        {
          int sec;
          if (int.TryParse(args[++i], out sec))
          {
            if (sec > 0) closeAfter = sec;
          }
        }
      }

        const string mutexName = "Global\\Gl30.EncoderViewer.SingleInstance";
      bool created;
      using (var mutex = new System.Threading.Mutex(true, mutexName, out created))
      {
        if (!created)
        {
          MessageBox.Show("EncoderViewer 已在运行中。", "EncoderViewer", MessageBoxButtons.OK, MessageBoxIcon.Information);
          return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new MainForm(new ViewerOptions
        {
          PortName = port,
          OutputDirectory = output,
          CapturePath = capture,
          CloseAfterSeconds = closeAfter
        }));
      }
    }
  }
}
