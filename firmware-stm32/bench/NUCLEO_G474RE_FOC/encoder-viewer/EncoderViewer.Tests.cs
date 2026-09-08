using System;
using System.Globalization;
using System.Collections.Generic;
using System.Linq;

namespace Gl30.EncoderViewer.Tests
{
  public static class TestRunner
  {
    private static int _passed;
    private static int _failed;

    private static readonly string[] RequiredFields = new[]
    {
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

    public static int Main(string[] args)
    {
      Run("TryParse_ValidStatusLine_ParsesKnownFields", TryParse_ValidStatusLine_ParsesKnownFields);
      Run("TryParse_RejectsNonStatusLines", TryParse_RejectsNonStatusLines);
      Run("TryParse_RejectsSingleMissingRequiredField", TryParse_RejectsSingleMissingRequiredField);
      Run("TryParse_RejectsMalformedValues", TryParse_RejectsMalformedValues);
      Run("TryParse_RejectsOutOfRangeEncoder", TryParse_RejectsOutOfRangeEncoder);
      Run("TryParse_RejectsDuplicateCriticalFields", TryParse_RejectsDuplicateCriticalFields);
      Run("TryParse_AcceptsUnsignedAndNegativeNumberFields", TryParse_AcceptsUnsignedAndNegativeNumberFields);
      Run("TryParse_SingleNegativeAgeIsInvalid", TryParse_SingleNegativeAgeIsInvalid);
      Run("EncoderValid_RequiresHealthAgeAndDiag", EncoderValid_RequiresHealthAgeAndDiag);
      Run("OutputOff_RequiresSafeControlState", OutputOff_RequiresSafeControlState);
      Run("AngleTracker_FirstPointReturnsFalseAndZero", AngleTracker_FirstPointReturnsFalseAndZero);
      Run("AngleTracker_UpdateAccumulatesUsingShortestDelta", AngleTracker_UpdateAccumulatesUsingShortestDelta);
      Run("AngleTracker_RejectsInvalidDeltasAndStaleSegments", AngleTracker_RejectsInvalidDeltasAndStaleSegments);
      Run("AngleTracker_RejectsNaNOrInfinityDelta", AngleTracker_RejectsNaNOrInfinityDelta);
      Run("AngleTracker_EncoderOkRollbackStartsNewSegment", AngleTracker_EncoderOkRollbackStartsNewSegment);
      Run("AngleTracker_EncoderOkCanStayEqual", AngleTracker_EncoderOkCanStayEqual);
      Run("AngleTracker_ResetClearsState", AngleTracker_ResetClearsState);
      Run("AngleTracker_InvalidRawThrows", AngleTracker_InvalidRawThrows);

      Console.WriteLine("已通过: " + _passed);
      Console.WriteLine("已失败: " + _failed);
      return _failed == 0 ? 0 : 1;
    }

    private static void Run(string name, Action test)
    {
      try
      {
        test();
        _passed++;
        Console.WriteLine("[PASS] " + name);
      }
      catch (Exception ex)
      {
        _failed++;
        Console.WriteLine("[FAIL] " + name + " : " + ex.Message);
      }
    }

    private static void AssertTrue(bool condition, string message)
    {
      if (!condition)
      {
        throw new Exception(message);
      }
    }

    private static void AssertFalse(bool condition, string message)
    {
      AssertTrue(!condition, message);
    }

    private static void AssertEqual<T>(T actual, T expected, string message)
    {
      if (!Equals(actual, expected))
      {
        throw new Exception(message + " (actual=" + actual + ", expected=" + expected + ")");
      }
    }

    private static void AssertNearlyEqual(double actual, double expected, double epsilon, string message)
    {
      if (double.IsNaN(actual) || double.IsInfinity(actual) || double.IsNaN(expected) || double.IsInfinity(expected))
      {
        throw new Exception(message + " (non-finite value)");
      }

      if (Math.Abs(actual - expected) > epsilon)
      {
        throw new Exception(message + " (actual=" + actual.ToString("R", CultureInfo.InvariantCulture) + ", expected=" + expected.ToString("R", CultureInfo.InvariantCulture) + ")");
      }
    }

    private static void AssertParseFailure(string line, string fieldHint, string message)
    {
      StatusSample sample;
      string error;
      bool ok = StatusSample.TryParse(line, out sample, out error);
      AssertFalse(ok, message);
      AssertTrue(sample == null, message + " sample should be null");
      AssertTrue(!string.IsNullOrEmpty(error), message + " error should be non-empty");
      if (!string.IsNullOrEmpty(fieldHint))
      {
        AssertTrue(error.IndexOf(fieldHint, StringComparison.OrdinalIgnoreCase) >= 0, message + " should mention " + fieldHint + " in error, got: " + error);
      }
    }

    private static StatusSample Parse(string line)
    {
      StatusSample sample;
      string error;
      bool ok = StatusSample.TryParse(line, out sample, out error);
      if (!ok)
      {
        throw new Exception("Parse failed: " + error);
      }
      return sample;
    }

    private static List<KeyValuePair<string, string>> StatusTemplate()
    {
      return new List<KeyValuePair<string, string>>
      {
        new KeyValuePair<string, string>("mode","0"),
        new KeyValuePair<string, string>("fault","0"),
        new KeyValuePair<string, string>("health","34"),
        new KeyValuePair<string, string>("moe","0"),
        new KeyValuePair<string, string>("off","1"),
        new KeyValuePair<string, string>("button","0"),
        new KeyValuePair<string, string>("vm_mv","1989"),
        new KeyValuePair<string, string>("ia_ma","-2242"),
        new KeyValuePair<string, string>("ib_ma","-2240"),
        new KeyValuePair<string, string>("ic_ma","-2238"),
        new KeyValuePair<string, string>("iq_ma","0"),
        new KeyValuePair<string, string>("id_ma","0"),
        new KeyValuePair<string, string>("enc","2186"),
        new KeyValuePair<string, string>("diag","510"),
        new KeyValuePair<string, string>("age_us","31"),
        new KeyValuePair<string, string>("adc","10822061"),
        new KeyValuePair<string, string>("enc_irq","2164412"),
        new KeyValuePair<string, string>("enc_ok","2164412"),
        new KeyValuePair<string, string>("enc_err","0"),
        new KeyValuePair<string, string>("zero","0"),
        new KeyValuePair<string, string>("self_left","0"),
        new KeyValuePair<string, string>("self_fail","0"),
        new KeyValuePair<string, string>("self_max","2461"),
        new KeyValuePair<string, string>("isr_max","3239"),
        new KeyValuePair<string, string>("deadline","0"),
        new KeyValuePair<string, string>("adc_bad","0"),
        new KeyValuePair<string, string>("uart_err","0"),
        new KeyValuePair<string, string>("drv","4294967295"),
        new KeyValuePair<string, string>("rate","1"),
        new KeyValuePair<string, string>("calibrated","0"),
        new KeyValuePair<string, string>("window_min","1031"),
        new KeyValuePair<string, string>("iwdg_reset","0"),
      };
    }

    private static string BuildStatusLine(string removeField = null, string replaceField = null, string replaceValue = null)
    {
      List<KeyValuePair<string, string>> fields = StatusTemplate();

      if (removeField != null)
      {
        for (int i = 0; i < fields.Count; ++i)
        {
          if (fields[i].Key == removeField)
          {
            fields.RemoveAt(i);
            break;
          }
        }
      }

      if (replaceField != null)
      {
        for (int i = 0; i < fields.Count; ++i)
        {
          if (fields[i].Key == replaceField)
          {
            fields[i] = new KeyValuePair<string, string>(fields[i].Key, replaceValue);
            break;
          }
        }
      }

      string[] tokens = new string[fields.Count];
      for (int i = 0; i < fields.Count; ++i)
      {
        tokens[i] = fields[i].Key + "=" + fields[i].Value;
      }
      return "STATUS " + string.Join(" ", tokens);
    }

    private static string BuildStatusLineWithDuplicate(string duplicateKey, string duplicateValue)
    {
      return BuildStatusLine() + " " + duplicateKey + "=" + duplicateValue;
    }

    private static void TryParse_ValidStatusLine_ParsesKnownFields()
    {
      StatusSample sample = Parse(BuildStatusLine());

      foreach (string field in RequiredFields)
      {
        AssertTrue(sample.Fields.ContainsKey(field), field + " missing from parsed fields");
      }

      AssertEqual(sample.Fields["enc"], 2186L, "enc wrong");
      AssertEqual(sample.Fields["diag"], 510L, "diag wrong");
      AssertEqual(sample.Fields["drv"], 4294967295L, "drv wrong");
      AssertEqual(sample.Fields["enc_err"], 0L, "enc_err wrong");
      AssertEqual(sample.Fields["uart_err"], 0L, "uart_err wrong");
      AssertEqual(sample.Fields["adc_bad"], 0L, "adc_bad wrong");
      AssertEqual(sample.Fields["deadline"], 0L, "deadline wrong");
      AssertEqual(sample.Fields["self_left"], 0L, "self_left wrong");
      AssertEqual(sample.Fields["self_fail"], 0L, "self_fail wrong");
      AssertEqual(sample.Fields["self_max"], 2461L, "self_max wrong");
      AssertEqual(sample.Fields["isr_max"], 3239L, "isr_max wrong");
      AssertNearlyEqual(sample.AngleDegrees, 2186.0 * (360.0 / 16384.0), 1e-9, "AngleDegrees wrong");
      AssertTrue(sample.EncoderValid, "EncoderValid should be true for normal line");
      AssertTrue(sample.OutputOff, "OutputOff should be true for normal line");
    }

    private static void TryParse_RejectsNonStatusLines()
    {
      StatusSample sample;
      string error;
      AssertFalse(StatusSample.TryParse("BOOT NUCLEO_G474RE_FOC TI_DRV8316REVM AS5048A 160MHZ PWM20K", out sample, out error), "BOOT must not parse");
      AssertTrue(sample == null, "sample should be null on non-status");
      AssertTrue(!string.IsNullOrEmpty(error), "error should be non-empty for non-status");
      AssertFalse(StatusSample.TryParse("ERR status parse fail", out sample, out error), "ERR must not parse");
      AssertTrue(sample == null, "sample should be null on ERR");
      AssertTrue(!string.IsNullOrEmpty(error), "error should be non-empty for ERR");
    }

    private static void TryParse_RejectsSingleMissingRequiredField()
    {
      foreach (string field in RequiredFields)
      {
        string line = BuildStatusLine(removeField: field);
        AssertParseFailure(line, field, "Missing required field '" + field + "' should fail");
      }
    }

    private static void TryParse_RejectsMalformedValues()
    {
      AssertParseFailure(BuildStatusLine(replaceField: "enc", replaceValue: "abc"), "enc", "Bad enc format should fail");
      AssertParseFailure(BuildStatusLine(replaceField: "drv", replaceValue: "abc"), "drv", "Bad drv format should fail");
      AssertParseFailure(BuildStatusLine(replaceField: "ia_ma", replaceValue: "12x"), "ia_ma", "Bad current format should fail");
      AssertParseFailure(BuildStatusLine(replaceField: "enc", replaceValue: "12=3"), "enc", "Bad enc token should fail");
      AssertParseFailure(BuildStatusLine(replaceField: "age_us", replaceValue: "10=1"), "age_us", "Bad age_us token should fail");
    }

    private static void TryParse_RejectsOutOfRangeEncoder()
    {
      AssertParseFailure(BuildStatusLine(replaceField: "enc", replaceValue: "-1"), "enc", "Negative enc should fail");
      AssertParseFailure(BuildStatusLine(replaceField: "enc", replaceValue: "16384"), "enc", "enc overflow high should fail");
      AssertParseFailure(BuildStatusLine(replaceField: "enc", replaceValue: "4294967296"), "enc", "enc outside uint16 should fail");
    }

    private static void TryParse_RejectsDuplicateCriticalFields()
    {
      AssertParseFailure(BuildStatusLineWithDuplicate("enc", "2186"), "enc", "Duplicate enc should fail");
      AssertParseFailure(BuildStatusLineWithDuplicate("diag", "510"), "diag", "Duplicate diag should fail");
      AssertParseFailure(BuildStatusLineWithDuplicate("enc_ok", "2164412"), "enc_ok", "Duplicate enc_ok should fail");
      AssertParseFailure(BuildStatusLineWithDuplicate("drv", "4294967295"), "drv", "Duplicate drv should fail");
    }

    private static void TryParse_AcceptsUnsignedAndNegativeNumberFields()
    {
      StatusSample sample = Parse(BuildStatusLine(replaceField: "drv", replaceValue: "4294967295"));
      AssertEqual(sample.Fields["drv"], 4294967295L, "drv max should parse into long");

      sample = Parse(BuildStatusLine(replaceField: "ia_ma", replaceValue: "-2242"));
      AssertEqual(sample.Fields["ia_ma"], -2242L, "ia_ma negative should parse");

      sample = Parse(BuildStatusLine(replaceField: "ib_ma", replaceValue: "-3001"));
      AssertEqual(sample.Fields["ib_ma"], -3001L, "ib_ma negative should parse");

      sample = Parse(BuildStatusLine(replaceField: "ic_ma", replaceValue: "-3002"));
      AssertEqual(sample.Fields["ic_ma"], -3002L, "ic_ma negative should parse");

      sample = Parse(BuildStatusLine(replaceField: "iq_ma", replaceValue: "-1"));
      AssertEqual(sample.Fields["iq_ma"], -1L, "iq_ma negative should parse");
    }

    private static void TryParse_SingleNegativeAgeIsInvalid()
    {
      StatusSample sample;
      string error;
      AssertFalse(StatusSample.TryParse(BuildStatusLine(replaceField: "age_us", replaceValue: "-1"), out sample, out error), "Negative age_us should be rejected");
      AssertTrue(sample == null, "Negative age_us sample should be null");
      AssertTrue(!string.IsNullOrEmpty(error), "Negative age_us should provide error");
    }

    private static void EncoderValid_RequiresHealthAgeAndDiag()
    {
      AssertFalse(Parse(BuildStatusLine(replaceField: "diag", replaceValue: "0")).EncoderValid, "Diag bit check failed: 0x000");
      AssertFalse(Parse(BuildStatusLine(replaceField: "diag", replaceValue: "768")).EncoderValid, "Diag 0x300 should be invalid");
      AssertFalse(Parse(BuildStatusLine(replaceField: "diag", replaceValue: "1280")).EncoderValid, "Diag 0x500 should be invalid");
      AssertFalse(Parse(BuildStatusLine(replaceField: "diag", replaceValue: "2304")).EncoderValid, "Diag 0x900 should be invalid");
      AssertFalse(Parse(BuildStatusLine(replaceField: "age_us", replaceValue: "751")).EncoderValid, "age_us > 750 should be invalid");
      AssertFalse(Parse(BuildStatusLine(replaceField: "health", replaceValue: "32")).EncoderValid, "health=32 should be invalid");
      AssertTrue(Parse(BuildStatusLine(replaceField: "age_us", replaceValue: "750")).EncoderValid, "health34/diag510/age750 should be valid");
    }

    private static void OutputOff_RequiresSafeControlState()
    {
      AssertTrue(Parse(BuildStatusLine()).OutputOff, "Safe control state should be off");
      AssertFalse(Parse(BuildStatusLine(replaceField: "mode", replaceValue: "1")).OutputOff, "mode=1 should not be off");
      AssertFalse(Parse(BuildStatusLine(replaceField: "fault", replaceValue: "1")).OutputOff, "fault=1 should not be off");
      AssertFalse(Parse(BuildStatusLine(replaceField: "moe", replaceValue: "1")).OutputOff, "moe=1 should not be off");
      AssertFalse(Parse(BuildStatusLine(replaceField: "off", replaceValue: "0")).OutputOff, "off=0 should not be off");
      AssertFalse(Parse(BuildStatusLine(replaceField: "calibrated", replaceValue: "1")).OutputOff, "calibrated=1 should not be off");
      AssertFalse(Parse(BuildStatusLine(replaceField: "button", replaceValue: "1")).OutputOff, "button=1 should not be off");
    }

    private static void AngleTracker_FirstPointReturnsFalseAndZero()
    {
      var tracker = new AngleTracker();
      bool changed = tracker.Update(1234, 0.1, 10);
      AssertFalse(changed, "first update should return false");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "first update should keep zero");
    }

    private static void AngleTracker_UpdateAccumulatesUsingShortestDelta()
    {
      var tracker = new AngleTracker();
      tracker.Update(400, 0.1, 1);
      bool changed = tracker.Update(401, 0.1, 2);
      AssertTrue(changed, "second update should apply delta");
      AssertNearlyEqual(tracker.RelativeDegrees, (1.0 * 360.0 / 16384.0), 1e-12, "forward delta wrong");

      changed = tracker.Update(399, 0.1, 3);
      AssertTrue(changed, "third update should apply reverse delta");
      AssertNearlyEqual(tracker.RelativeDegrees, (-1.0 * 360.0 / 16384.0), 1e-12, "shortest reverse continuation wrong");

      tracker.Reset();
      tracker.Update(16380, 0.1, 1);
      changed = tracker.Update(4, 0.1, 2);
      AssertTrue(changed, "wrap forward across zero should apply shortest path");
      AssertNearlyEqual(tracker.RelativeDegrees, (8.0 * 360.0 / 16384.0), 1e-12, "wrap forward shortest path wrong");

      changed = tracker.Update(16380, 0.1, 3);
      AssertTrue(changed, "wrap reverse across zero should apply shortest path");
      AssertNearlyEqual(tracker.RelativeDegrees, (0.0 * 360.0 / 16384.0), 1e-12, "wrap reverse should reduce by 8 counts");
    }

    private static void AngleTracker_RejectsInvalidDeltasAndStaleSegments()
    {
      var tracker = new AngleTracker();
      tracker.Update(100, 0.1, 10);
      AssertFalse(tracker.Update(120, 0.0, 11), "dt==0 should reject");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "dt==0 should reset relative");
      AssertFalse(tracker.Update(140, -0.001, 12), "dt<0 should reject");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "dt<0 should reset relative");
      AssertFalse(tracker.Update(160, 0.6, 13), "stale interval should reject");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "stale interval should reset relative");
    }

    private static void AngleTracker_RejectsNaNOrInfinityDelta()
    {
      var tracker = new AngleTracker();
      tracker.Update(100, 0.1, 10);
      tracker.Update(200, 0.2, 11);
      AssertNearlyEqual(tracker.RelativeDegrees, (100.0 * 360.0 / 16384.0), 1e-12, "base accumulation wrong");

      AssertFalse(tracker.Update(201, double.NaN, 12), "NaN deltaSeconds should be rejected");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "NaN should reset relative");

      tracker.Update(251, 0.1, 13);
      AssertNearlyEqual(tracker.RelativeDegrees, (50.0 * 360.0 / 16384.0), 1e-12, "relative should resume after NaN reset");

      AssertFalse(tracker.Update(252, double.PositiveInfinity, 14), "Infinity deltaSeconds should be rejected");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "Infinity should reset relative");
      AssertTrue(tracker.Update(262, 0.1, 14), "continuation should resume after Infinity reset");
      AssertNearlyEqual(tracker.RelativeDegrees, (10.0 * 360.0 / 16384.0), 1e-12, "relative should continue from new segment");
    }

    private static void AngleTracker_EncoderOkRollbackStartsNewSegment()
    {
      var tracker = new AngleTracker();
      tracker.Update(100, 0.1, 100);
      AssertFalse(tracker.Update(200, 0.1, 99), "rollback encoderOk should reject and reset");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "rollback should reset relative");
      bool changed = tracker.Update(300, 0.1, 99);
      AssertTrue(changed, "new segment should continue after rollback point");
      changed = tracker.Update(320, 0.1, 99);
      AssertTrue(changed, "post rollback continuity should continue");
    }

    private static void AngleTracker_EncoderOkCanStayEqual()
    {
      var tracker = new AngleTracker();
      tracker.Update(1000, 0.1, 1234);
      bool changed = tracker.Update(1001, 0.1, 1234);
      AssertTrue(changed, "equal encoderOk should be treated as continuous");
      AssertNearlyEqual(tracker.RelativeDegrees, (1.0 * 360.0 / 16384.0), 1e-12, "equal encoderOk delta wrong");
    }

    private static void AngleTracker_ResetClearsState()
    {
      var tracker = new AngleTracker();
      tracker.Update(100, 0.1, 1);
      tracker.Update(500, 0.1, 2);
      tracker.Reset();
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "reset should clear relative");
      bool changed = tracker.Update(1000, 0.1, 3);
      AssertFalse(changed, "first point after reset should return false");
      AssertNearlyEqual(tracker.RelativeDegrees, 0.0, 1e-12, "first point after reset should be zero");
    }

    private static void AngleTracker_InvalidRawThrows()
    {
      var tracker = new AngleTracker();
      bool thrown;

      thrown = false;
      try
      {
        tracker.Update(-1, 0.1, 1);
      }
      catch (ArgumentOutOfRangeException)
      {
        thrown = true;
      }
      AssertTrue(thrown, "negative raw should throw");

      thrown = false;
      try
      {
        tracker.Update(16384, 0.1, 1);
      }
      catch (ArgumentOutOfRangeException)
      {
        thrown = true;
      }
      AssertTrue(thrown, "raw>16383 should throw");
    }
  }
}
