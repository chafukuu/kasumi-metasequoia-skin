// KasumiBandOverlay - an independent, band-only layered helper for the
// official Metasequoia candidate window.
//
// The resident process never changes the target window's region, memory,
// pixels, styles, or input state. It owns one WS_EX_LAYERED tool window,
// paints only the pre-cropped band into it with UpdateLayeredWindow, and
// inserts that window immediately behind the strictly identified official
// candidate host. It does not import SetWindowRgn.
//
// C# 5 / .NET Framework 4.x. The resident executable is compiled with a
// requireAdministrator + PerMonitorV2 manifest. The diagnostics executable
// is compiled asInvoker and can only run --self-test or --probe.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.AccessControl;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace KasumiBandOverlay
{
    internal sealed class Options
    {
        internal string SkinDir;
        internal string DataDir;
        internal string DiagnosticLog = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "水杉IME诊断日志.log");
        internal bool SelfTest;
        internal bool Probe;
        internal bool ReloadSkin;
        internal bool ReplaceRunning;

        internal static Options Parse(string[] args)
        {
            Options options = new Options();
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i];
                if (arg == "--self-test") { options.SelfTest = true; continue; }
                if (arg == "--probe") { options.Probe = true; continue; }
                if (arg == "--reload-skin") { options.ReloadSkin = true; continue; }
                if (arg == "--replace-running") { options.ReplaceRunning = true; continue; }
                if (i + 1 >= args.Length) throw new ArgumentException("Missing option value");
                string value = args[++i];
                if (arg == "--skin-dir") options.SkinDir = Path.GetFullPath(value);
                else if (arg == "--data-dir") options.DataDir = Path.GetFullPath(value);
                else if (arg == "--diagnostic-log") options.DiagnosticLog = Path.GetFullPath(value);
                else throw new ArgumentException("Unknown option");
            }
            if (!options.SelfTest && !options.Probe &&
                (options.SkinDir == null || options.DataDir == null))
                throw new ArgumentException("Required: --skin-dir PATH --data-dir PATH [--diagnostic-log PATH] [--reload-skin] [--replace-running]");
            return options;
        }
    }

    internal static class GuardedFiles
    {
        internal static string SafeFile(string root, string relative)
        {
            if (String.IsNullOrEmpty(root)) throw new InvalidDataException("Skin directory is required");
            if (String.IsNullOrEmpty(relative) || Path.IsPathRooted(relative) || relative.IndexOf(':') >= 0)
                throw new InvalidDataException("Relative skin file required");
            string basePath = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string result = Path.GetFullPath(Path.Combine(basePath, relative));
            if (!result.StartsWith(basePath + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Skin file escapes root");
            // Reject junctions/symlinks in every component, including the root.
            for (string path = result; !String.IsNullOrEmpty(path); path = Path.GetDirectoryName(path))
                if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidDataException("Reparse-point skin paths are not accepted");
            return result;
        }

        internal static string ReadSmall(string file, int limit)
        {
            using (FileStream stream = new FileStream(file, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            {
                if (stream.Length > limit) throw new InvalidDataException("File too large");
                using (StreamReader reader = new StreamReader(stream, new UTF8Encoding(false, true), true))
                    return reader.ReadToEnd();
            }
        }

        internal static string Stamp(string file)
        {
            FileInfo info = new FileInfo(file);
            if (!info.Exists) throw new FileNotFoundException("File not found", file);
            return info.Length.ToString(CultureInfo.InvariantCulture) + ":" +
                info.LastWriteTimeUtc.Ticks.ToString(CultureInfo.InvariantCulture);
        }
    }

    internal static class SkinSelection
    {
        internal static bool IsSelected(string dataDir)
        {
            string file = GuardedFiles.SafeFile(dataDir, "config.toml");
            return ParseSelected(GuardedFiles.ReadSmall(file, 1024 * 1024));
        }

        internal static bool ParseSelected(string text)
        {
            bool selected = false, found = false, diagnostic = false, diagnosticFound = false;
            string section = "", multiline = null;
            using (StringReader reader = new StringReader(text))
            {
                string line;
                while ((line = reader.ReadLine()) != null)
                {
                    string trimmed = ConfigLine(line, ref multiline).Trim();
                    if (trimmed.StartsWith("[", StringComparison.Ordinal))
                    {
                        if (Regex.IsMatch(trimmed, @"^\[general\]\s*$")) section = "general";
                        else if (Regex.IsMatch(trimmed, @"^\[appearance\]\s*$")) section = "appearance";
                        else section = "";
                        continue;
                    }
                    if (section == "general" && Regex.IsMatch(trimmed, @"^diagnostic_log\s*="))
                    {
                        if (diagnosticFound) return false;
                        diagnosticFound = true;
                        diagnostic = Regex.IsMatch(trimmed, @"^diagnostic_log\s*=\s*true\s*$");
                        continue;
                    }
                    if (section != "appearance" || !Regex.IsMatch(trimmed, @"^candidate_skin\s*=")) continue;
                    if (found) return false;
                    found = true;
                    selected = Regex.IsMatch(trimmed, "^candidate_skin\\s*=\\s*(?:\"kasumi-adaptive\"|'kasumi-adaptive')\\s*$");
                }
            }
            return found && selected && diagnosticFound && diagnostic && multiline == null;
        }

        private static string ConfigLine(string line, ref string multiline)
        {
            if (multiline != null)
            {
                int end = line.IndexOf(multiline, StringComparison.Ordinal);
                if (end < 0) return "";
                multiline = null;
                return "";
            }
            char quote = '\0';
            bool escaped = false;
            for (int i = 0; i < line.Length; i++)
            {
                char c = line[i];
                if (quote != '\0')
                {
                    if (quote == '"' && escaped) { escaped = false; continue; }
                    if (quote == '"' && c == '\\') { escaped = true; continue; }
                    if (c == quote) quote = '\0';
                    continue;
                }
                if (c == '#') return line.Substring(0, i);
                if (c == '"' || c == '\'')
                {
                    if (i + 2 < line.Length && line[i + 1] == c && line[i + 2] == c)
                    {
                        string marker = new string(c, 3);
                        int end = line.IndexOf(marker, i + 3, StringComparison.Ordinal);
                        if (end < 0) { multiline = marker; return line.Substring(0, i); }
                        i = end + 2;
                    }
                    else quote = c;
                }
            }
            return line;
        }
    }

    internal sealed class OverlayConfig
    {
        internal const string FileName = "band-overlay.json";
        internal const string SelectedSkin = "kasumi-adaptive";
        internal string SkinDir;
        internal string ConfigPath;
        internal string ImageRelative;
        internal string ImagePath;
        internal double WidthDip;
        internal double HeightDip;
        internal double OffsetXDip;
        internal double OffsetYDip;
        internal double CutOffsetXDip;
        internal FretSpec Frets;
        internal AtlasSpec Atlas;
        internal string SourceStamp;
        internal string Fingerprint;

        internal static OverlayConfig Load(string skinDir)
        {
            string fullSkin = Path.GetFullPath(skinDir);
            string configPath = GuardedFiles.SafeFile(fullSkin, FileName);
            string text = GuardedFiles.ReadSmall(configPath, 65536);
            return Parse(text, fullSkin, configPath);
        }

        internal static OverlayConfig ParseForDiagnostics(string text, string skinDir, string configPath)
        {
            return Parse(text, Path.GetFullPath(skinDir), configPath);
        }

        private static OverlayConfig Parse(string text, string skinDir, string configPath)
        {
            Dictionary<string, object> map = ReadDictionary(text);
            if (map == null) throw new InvalidDataException("band-overlay.json object required");
            if (Integer(map, "schemaVersion") != 1)
                throw new InvalidDataException("band-overlay.json schemaVersion=1 required");
            object fixedSkin;
            if (!map.TryGetValue("selectedSkin", out fixedSkin) || !(fixedSkin is string) ||
                !String.Equals((string)fixedSkin, SelectedSkin, StringComparison.Ordinal))
                throw new InvalidDataException("selectedSkin must be kasumi-adaptive");
            OverlayConfig config = new OverlayConfig();
            config.SkinDir = Path.GetFullPath(skinDir);
            config.ConfigPath = configPath;
            config.ImageRelative = RequiredText(map, "image");
            config.WidthDip = RequiredNumber(map, "widthDip", 1, 4096);
            config.HeightDip = RequiredNumber(map, "heightDip", 1, 2048);
            config.OffsetXDip = RequiredNumber(map, "offsetXDip", -4096, 4096);
            config.OffsetYDip = RequiredNumber(map, "offsetYDip", -4096, 4096);
            config.CutOffsetXDip = RequiredNumber(map, "cutOffsetXDip", 0, 256);
            bool fretsPresent;
            object frets = ReadOptionalObjectProperty(text, "frets", out fretsPresent);
            if (fretsPresent) config.Frets = FretSpec.Parse(frets);
            bool atlasPresent;
            object atlas = ReadOptionalObjectProperty(text, "atlas", out atlasPresent);
            if (atlasPresent)
            {
                Dictionary<string, object> atlasMap = atlas as Dictionary<string, object>;
                if (atlasMap == null) throw new InvalidDataException("atlas object required");
                config.Atlas = AtlasSpec.Parse(atlasMap, config.SkinDir);
                config.Atlas.ValidateDesign(config.WidthDip, config.HeightDip);
            }
            config.ImagePath = GuardedFiles.SafeFile(config.SkinDir, config.ImageRelative);
            string sourceStamp = (File.Exists(configPath) ? GuardedFiles.Stamp(configPath) : "diagnostics") +
                "|" + GuardedFiles.Stamp(config.ImagePath);
            if (config.Atlas != null) sourceStamp += "|" + config.Atlas.ExcludeStamp + "|" + config.Atlas.RestoreStamp + "|" + config.Atlas.ForegroundStamp + "|" + config.Atlas.PortraitStamp;
            config.SourceStamp = sourceStamp;
            config.Fingerprint = text + "|" + config.SourceStamp;
            return config;
        }

        internal void ValidateAsset(BandAsset asset)
        {
            if (asset == null) throw new ArgumentNullException("asset");
            if (Atlas != null)
            {
                Atlas.ValidateAsset(asset.Width, asset.Height);
                if (Atlas.OcclusionMode) asset.PrepareOcclusionSource(Atlas);
            }
        }

        private static Dictionary<string, object> ReadDictionary(string text)
        {
            try
            {
                using (MemoryStream bytes = new MemoryStream(Encoding.UTF8.GetBytes(text)))
                    return new DataContractJsonSerializer(typeof(Dictionary<string, object>),
                        new DataContractJsonSerializerSettings { UseSimpleDictionaryFormat = true })
                        .ReadObject(bytes) as Dictionary<string, object>;
            }
            catch (Exception ex)
            {
                throw new InvalidDataException("band-overlay.json is not valid JSON", ex);
            }
        }

        // DataContractJsonSerializer leaves arbitrary nested objects as System.Object.
        // Locate only the top-level "frets" value in the already validated JSON and
        // deserialize that small primitive object separately.
        private static object ReadOptionalObjectProperty(string json, string propertyName, out bool present)
        {
            present = false;
            int depth = 0;
            for (int i = 0; i < json.Length; i++)
            {
                char c = json[i];
                if (c == '"')
                {
                    int tokenStart = i + 1, j = tokenStart;
                    bool escaped = false;
                    for (; j < json.Length; j++)
                    {
                        char inner = json[j];
                        if (escaped) { escaped = false; continue; }
                        if (inner == '\\') { escaped = true; continue; }
                        if (inner == '"') break;
                    }
                    if (j >= json.Length) throw new InvalidDataException("Unterminated JSON string");
                    string token = json.Substring(tokenStart, j - tokenStart);
                    int colon = j + 1;
                    while (colon < json.Length && Char.IsWhiteSpace(json[colon])) colon++;
                    if (depth == 1 && String.Equals(token, propertyName, StringComparison.Ordinal) &&
                        colon < json.Length && json[colon] == ':')
                    {
                        present = true;
                        int valueStart = colon + 1;
                        while (valueStart < json.Length && Char.IsWhiteSpace(json[valueStart])) valueStart++;
                        if (valueStart >= json.Length || json[valueStart] != '{')
                            throw new InvalidDataException("frets object required");
                        int valueDepth = 0;
                        bool inString = false, stringEscape = false;
                        for (int k = valueStart; k < json.Length; k++)
                        {
                            char v = json[k];
                            if (inString)
                            {
                                if (stringEscape) { stringEscape = false; continue; }
                                if (v == '\\') { stringEscape = true; continue; }
                                if (v == '"') inString = false;
                                continue;
                            }
                            if (v == '"') { inString = true; continue; }
                            if (v == '{') valueDepth++;
                            else if (v == '}')
                            {
                                valueDepth--;
                                if (valueDepth == 0)
                                {
                                    string fragment = json.Substring(valueStart, k - valueStart + 1);
                                    return ReadDictionary(fragment);
                                }
                            }
                        }
                        throw new InvalidDataException("Unterminated frets object");
                    }
                    i = j;
                    continue;
                }
                if (c == '{') depth++;
                else if (c == '}') depth--;
            }
            return null;
        }

        internal bool FilesUnchanged()
        {
            try
            {
                string stamp = (File.Exists(ConfigPath) ? GuardedFiles.Stamp(ConfigPath) : "diagnostics") +
                    "|" + GuardedFiles.Stamp(ImagePath);
                if (Atlas != null) stamp += "|" + GuardedFiles.Stamp(Atlas.ExcludePath) + "|" +
                    (Atlas.RestorePath == null ? "none" : GuardedFiles.Stamp(Atlas.RestorePath)) + "|" +
                    (Atlas.ForegroundPath == null ? "none" : GuardedFiles.Stamp(Atlas.ForegroundPath)) + "|" +
                    (Atlas.PortraitPath == null ? "none" : GuardedFiles.Stamp(Atlas.PortraitPath));
                return String.Equals(stamp, SourceStamp, StringComparison.Ordinal);
            }
            catch { return false; }
        }

        private static double RequiredNumber(Dictionary<string, object> map, string key, double min, double max)
        {
            object value;
            if (!map.TryGetValue(key, out value))
                throw new InvalidDataException("Missing numeric band-overlay property: " + key);
            if (!(value is int || value is long || value is decimal || value is double || value is float))
                throw new InvalidDataException("Numeric band-overlay property required");
            double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
            if (Double.IsNaN(number) || Double.IsInfinity(number) || number < min || number > max)
                throw new InvalidDataException("Band-overlay property outside bounds");
            return number;
        }

        private static int Integer(Dictionary<string, object> map, string key)
        {
            object value;
            if (!map.TryGetValue(key, out value) ||
                !(value is int || value is long || value is decimal || value is double || value is float))
                throw new InvalidDataException("Numeric schemaVersion required");
            double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
            if (Double.IsNaN(number) || Double.IsInfinity(number) || number != Math.Floor(number))
                throw new InvalidDataException("Integer schemaVersion required");
            return checked((int)number);
        }

        private static string RequiredText(Dictionary<string, object> map, string key)
        {
            object value;
            if (!map.TryGetValue(key, out value) || !(value is string) || String.IsNullOrWhiteSpace((string)value))
                throw new InvalidDataException("String band-overlay property required: " + key);
            return (string)value;
        }
    }

    internal sealed class FretSpec
    {
        internal readonly double CardWidthDip, ToleranceDip, StartXDip, PitchDip;
        internal readonly double TopDip, BottomDip, LineWidthDip, LineGapDip;
        internal readonly int Count, ColorRgb;
        internal readonly string ColorText, Key;
        internal bool FollowNativeItems;
        private static readonly Regex ColorPattern = new Regex("^#[0-9A-Fa-f]{6}$", RegexOptions.CultureInvariant);

        private FretSpec(double cardWidthDip, double toleranceDip, double startXDip, double pitchDip,
            int count, double topDip, double bottomDip, double lineWidthDip, double lineGapDip,
            int colorRgb, string colorText)
        {
            CardWidthDip = cardWidthDip; ToleranceDip = toleranceDip;
            StartXDip = startXDip; PitchDip = pitchDip; Count = count;
            TopDip = topDip; BottomDip = bottomDip;
            LineWidthDip = lineWidthDip; LineGapDip = lineGapDip;
            ColorRgb = colorRgb; ColorText = colorText;
            Key = "card=" + cardWidthDip.ToString("R", CultureInfo.InvariantCulture) +
                ";tol=" + toleranceDip.ToString("R", CultureInfo.InvariantCulture) +
                ";start=" + startXDip.ToString("R", CultureInfo.InvariantCulture) +
                ";pitch=" + pitchDip.ToString("R", CultureInfo.InvariantCulture) +
                ";count=" + count.ToString(CultureInfo.InvariantCulture) +
                ";top=" + topDip.ToString("R", CultureInfo.InvariantCulture) +
                ";bottom=" + bottomDip.ToString("R", CultureInfo.InvariantCulture) +
                ";width=" + lineWidthDip.ToString("R", CultureInfo.InvariantCulture) +
                ";gap=" + lineGapDip.ToString("R", CultureInfo.InvariantCulture) +
                ";color=#" + colorRgb.ToString("X6", CultureInfo.InvariantCulture);
        }

        internal static FretSpec Parse(object raw)
        {
            Dictionary<string, object> map = raw as Dictionary<string, object>;
            if (map == null) throw new InvalidDataException("frets object required");
            double cardWidthDip = Number(map, "cardWidthDip", 1, 4096);
            double toleranceDip = Number(map, "toleranceDip", 0, 256);
            double startXDip = Number(map, "startXDip", 0, 4096);
            double pitchDip = Number(map, "pitchDip", 0.1, 4096);
            int count = Integer(map, "count", 0, 20);
            double topDip = Number(map, "topDip", 0, 2048);
            double bottomDip = Number(map, "bottomDip", 0, 2048);
            double lineWidthDip = Number(map, "lineWidthDip", 0.05, 64);
            double lineGapDip = Number(map, "lineGapDip", 0.1, 64);
            if (bottomDip <= topDip) throw new InvalidDataException("frets bottomDip must exceed topDip");
            string color = Text(map, "color");
            if (!ColorPattern.IsMatch(color)) throw new InvalidDataException("frets color must be #RRGGBB");
            int colorRgb = Int32.Parse(color.Substring(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            double outerHalfDip = (lineGapDip + lineWidthDip) * 0.5;
            for (int i = 0; i < count; i++)
            {
                double center = startXDip + pitchDip * (i + 1);
                if (center - outerHalfDip < 0 || center + outerHalfDip > cardWidthDip)
                    throw new InvalidDataException("frets boundary outside declared card width");
            }
            FretSpec spec = new FretSpec(cardWidthDip, toleranceDip, startXDip, pitchDip, count,
                topDip, bottomDip, lineWidthDip, lineGapDip, colorRgb, color);
            object follow;
            if (map.TryGetValue("followNativeItems", out follow))
            {
                if (!(follow is bool)) throw new InvalidDataException("followNativeItems must be boolean");
                spec.FollowNativeItems = (bool)follow;
            }
            return spec;
        }

        private static double Number(Dictionary<string, object> map, string key, double min, double max)
        {
            object value;
            if (!map.TryGetValue(key, out value))
                throw new InvalidDataException("Missing numeric frets property: " + key);
            if (!(value is int || value is long || value is decimal || value is double || value is float))
                throw new InvalidDataException("Numeric frets property required");
            double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
            if (Double.IsNaN(number) || Double.IsInfinity(number) || number < min || number > max)
                throw new InvalidDataException("Frets property outside bounds");
            return number;
        }

        private static int Integer(Dictionary<string, object> map, string key, int min, int max)
        {
            double number = Number(map, key, min, max);
            if (number != Math.Floor(number)) throw new InvalidDataException("Integer frets property required");
            return checked((int)number);
        }

        private static string Text(Dictionary<string, object> map, string key)
        {
            object value;
            if (!map.TryGetValue(key, out value) || !(value is string) || String.IsNullOrWhiteSpace((string)value))
                throw new InvalidDataException("String frets property required: " + key);
            return (string)value;
        }
    }

    internal sealed class AtlasSpec
    {
        internal readonly int SourceWidthPx, SourceHeightPx;
        internal readonly int LeftFixedEndPx, EmblemStartPx, EmblemEndPx;
        internal readonly int RightFixedStartPx, CardRightPx;
        internal readonly int[][] ExcludeRuns;
        internal readonly string ExcludeRelative, ExcludePath, ExcludeStamp;
        internal int[][] RestoreWhiteRuns = new int[0][];
        internal string RestorePath;
        internal string RestoreStamp = "none";
        internal bool OcclusionMode;
        internal int OcclusionCutPx;
        internal int[][] ForegroundRuns = new int[0][];
        internal string ForegroundPath;
        internal string ForegroundStamp = "none";
        internal int[][] PortraitRuns = new int[0][];
        internal string PortraitPath, PortraitStamp = "none";
        internal double DesignScaleDip = 1.0;
        internal double ForegroundScale = 1.0;
        internal double ForegroundAnchorYPx = 420.0;
        internal double ForegroundOffsetYDip;
        internal int BackgroundRepairSourceX = -1, BackgroundRepairTargetX;
        internal int BackgroundRepairTop, BackgroundRepairBottom;
        internal int BackgroundRemovalMarginPx;
        internal string Key;

        internal AtlasSpec(int sourceWidthPx, int sourceHeightPx, int leftFixedEndPx,
            int emblemStartPx, int emblemEndPx, int rightFixedStartPx, int cardRightPx,
            int[][] excludeRuns, string excludeRelative, string excludePath, string excludeStamp)
        {
            if (sourceWidthPx < 1 || sourceWidthPx > 16384 || sourceHeightPx < 1 || sourceHeightPx > 8192)
                throw new InvalidDataException("Atlas source dimensions outside bounds");
            if (leftFixedEndPx <= 0 || emblemStartPx <= leftFixedEndPx ||
                emblemEndPx <= emblemStartPx || rightFixedStartPx <= emblemEndPx ||
                cardRightPx <= rightFixedStartPx || cardRightPx > sourceWidthPx)
                throw new InvalidDataException("Atlas slices must increase inside the source");
            int fixedBudget = leftFixedEndPx + (emblemEndPx - emblemStartPx) +
                (sourceWidthPx - rightFixedStartPx);
            int elasticSource = (emblemStartPx - leftFixedEndPx) +
                (rightFixedStartPx - emblemEndPx);
            if (fixedBudget <= 0 || elasticSource <= 0 || fixedBudget >= sourceWidthPx)
                throw new InvalidDataException("Atlas fixed budget does not fit");
            if (excludeRuns == null) throw new InvalidDataException("Atlas exclusion list required");
            if (excludeRuns.Length > 200000) throw new InvalidDataException("Atlas exclusion list too large");
            for (int i = 0; i < excludeRuns.Length; i++)
            {
                int[] run = excludeRuns[i];
                if (run == null || run.Length != 3 ||
                    run[0] < 0 || run[0] >= sourceHeightPx ||
                    run[1] < 0 || run[1] >= run[2] || run[2] > sourceWidthPx)
                    throw new InvalidDataException("Atlas exclusion run outside source");
            }

            SourceWidthPx = sourceWidthPx; SourceHeightPx = sourceHeightPx;
            LeftFixedEndPx = leftFixedEndPx; EmblemStartPx = emblemStartPx; EmblemEndPx = emblemEndPx;
            RightFixedStartPx = rightFixedStartPx; CardRightPx = cardRightPx;
            ExcludeRuns = excludeRuns; ExcludeRelative = excludeRelative;
            ExcludePath = excludePath; ExcludeStamp = excludeStamp;
            Key = "src=" + sourceWidthPx.ToString(CultureInfo.InvariantCulture) + "x" +
                sourceHeightPx.ToString(CultureInfo.InvariantCulture) +
                ";left=" + leftFixedEndPx.ToString(CultureInfo.InvariantCulture) +
                ";emblemStart=" + emblemStartPx.ToString(CultureInfo.InvariantCulture) +
                ";emblemEnd=" + emblemEndPx.ToString(CultureInfo.InvariantCulture) +
                ";rightStart=" + rightFixedStartPx.ToString(CultureInfo.InvariantCulture) +
                ";cardRight=" + cardRightPx.ToString(CultureInfo.InvariantCulture) +
                ";exclude=" + (excludeStamp == null ? "none" : excludeStamp);
        }

        internal int FixedBudgetSourcePx
        {
            get
            {
                return LeftFixedEndPx + (EmblemEndPx - EmblemStartPx) +
                    (SourceWidthPx - RightFixedStartPx);
            }
        }

        internal int ElasticSourcePx
        {
            get
            {
                return (EmblemStartPx - LeftFixedEndPx) +
                    (RightFixedStartPx - EmblemEndPx);
            }
        }

        internal static AtlasSpec Parse(Dictionary<string, object> map, string skinDir)
        {
            int sourceWidthPx = Integer(map, "sourceWidthPx", 1, 16384);
            int sourceHeightPx = Integer(map, "sourceHeightPx", 1, 8192);
            int leftFixedEndPx = Integer(map, "leftFixedEndPx", 1, 16384);
            int emblemStartPx = Integer(map, "emblemStartPx", 1, 16384);
            int emblemEndPx = Integer(map, "emblemEndPx", 1, 16384);
            int rightFixedStartPx = Integer(map, "rightFixedStartPx", 1, 16384);
            int cardRightPx = Integer(map, "cardRightPx", 1, 16384);
            string excludeRelative = Text(map, "excludeRuns");
            string excludePath = GuardedFiles.SafeFile(skinDir, excludeRelative);
            string excludeStamp = GuardedFiles.Stamp(excludePath);
            int[][] runs = ReadExclusionRuns(excludePath);
            AtlasSpec result = new AtlasSpec(sourceWidthPx, sourceHeightPx, leftFixedEndPx,
                emblemStartPx, emblemEndPx, rightFixedStartPx, cardRightPx,
                runs, excludeRelative, excludePath, excludeStamp);
            object restoreValue;
            if (map.TryGetValue("restoreWhiteRuns", out restoreValue))
            {
                if (!(restoreValue is string)) throw new InvalidDataException("restoreWhiteRuns path required");
                result.RestorePath = GuardedFiles.SafeFile(skinDir, (string)restoreValue);
                result.RestoreStamp = GuardedFiles.Stamp(result.RestorePath);
                result.RestoreWhiteRuns = ReadExclusionRuns(result.RestorePath);
                if (result.RestoreWhiteRuns == null || result.RestoreWhiteRuns.Length > 200000)
                    throw new InvalidDataException("Invalid white restoration runs");
                foreach (int[] run in result.RestoreWhiteRuns)
                    if (run == null || run.Length != 3 || run[0] < 0 || run[0] >= sourceHeightPx ||
                        run[1] < 0 || run[1] >= run[2] || run[2] > sourceWidthPx)
                        throw new InvalidDataException("White restoration run outside source");
            }
            result.Key += ";whiteRestore=" + result.RestoreStamp;
            object mode;
            if (map.TryGetValue("mode", out mode))
            {
                if (!(mode is string) || (string)mode != "occlusion")
                    throw new InvalidDataException("Unknown atlas mode");
                result.OcclusionMode = true;
                result.OcclusionCutPx = Integer(map, "occlusionCutPx", 1, sourceWidthPx);
                result.ForegroundPath = GuardedFiles.SafeFile(skinDir, Text(map, "foregroundKeepRuns"));
                result.ForegroundStamp = GuardedFiles.Stamp(result.ForegroundPath);
                result.ForegroundRuns = ReadExclusionRuns(result.ForegroundPath);
                if (result.ForegroundRuns == null || result.ForegroundRuns.Length == 0 || result.ForegroundRuns.Length > 200000)
                    throw new InvalidDataException("Foreground runs required");
                foreach (int[] run in result.ForegroundRuns)
                    if (run == null || run.Length != 3 || run[0] < 0 || run[0] >= sourceHeightPx ||
                        run[1] < 0 || run[1] >= run[2] || run[2] > sourceWidthPx)
                        throw new InvalidDataException("Foreground run outside source");
            }
            result.ForegroundScale = OptionalNumber(map, "foregroundScale", 1.0, 0.5, 1.5);
            if (map.ContainsKey("portraitKeepRuns"))
            {
                result.PortraitPath = GuardedFiles.SafeFile(skinDir, Text(map, "portraitKeepRuns"));
                result.PortraitStamp = GuardedFiles.Stamp(result.PortraitPath);
                result.PortraitRuns = ReadExclusionRuns(result.PortraitPath);
                if (result.PortraitRuns == null || result.PortraitRuns.Length == 0 || result.PortraitRuns.Length > 200000)
                    throw new InvalidDataException("Portrait runs required");
                foreach (int[] run in result.PortraitRuns)
                    if (run == null || run.Length != 3 || run[0] < 0 || run[0] >= sourceHeightPx ||
                        run[1] < 0 || run[1] >= run[2] || run[2] > sourceWidthPx)
                        throw new InvalidDataException("Portrait run outside source");
            }
            result.BackgroundRemovalMarginPx = (int)OptionalNumber(map, "backgroundRemovalMarginPx", 0, 0, 4);
            result.ForegroundAnchorYPx = OptionalNumber(map, "foregroundAnchorYPx", 420, 0, sourceHeightPx);
            result.ForegroundOffsetYDip = OptionalNumber(map, "foregroundOffsetYDip", 0, -256, 256);
            if (map.ContainsKey("backgroundRepairSourceXPx"))
            {
                result.BackgroundRepairSourceX = Integer(map, "backgroundRepairSourceXPx", 0, sourceWidthPx - 1);
                result.BackgroundRepairTargetX = Integer(map, "backgroundRepairTargetXPx", 1, sourceWidthPx - 1);
                result.BackgroundRepairTop = Integer(map, "backgroundRepairTopPx", 0, sourceHeightPx - 1);
                result.BackgroundRepairBottom = Integer(map, "backgroundRepairBottomPx", 1, sourceHeightPx);
                if (result.BackgroundRepairBottom <= result.BackgroundRepairTop ||
                    result.BackgroundRepairSourceX + sourceWidthPx - result.BackgroundRepairTargetX > sourceWidthPx)
                    throw new InvalidDataException("Background repair source rectangle outside artwork");
            }
            result.Key += ";occlusion=" + result.OcclusionMode + ";cut=" + result.OcclusionCutPx + ";foreground=" + result.ForegroundStamp +
                ";fgScale=" + result.ForegroundScale.ToString("R", CultureInfo.InvariantCulture) +
                ";fgAnchor=" + result.ForegroundAnchorYPx.ToString("R", CultureInfo.InvariantCulture) +
                ";fgY=" + result.ForegroundOffsetYDip.ToString("R", CultureInfo.InvariantCulture) +
                ";repair=" + result.BackgroundRepairSourceX + "," + result.BackgroundRepairTargetX +
                "," + result.BackgroundRepairTop + "," + result.BackgroundRepairBottom + "," + result.BackgroundRemovalMarginPx +
                ";portrait=" + result.PortraitStamp;
            return result;
        }

        private static double OptionalNumber(Dictionary<string, object> map, string key, double fallback, double min, double max)
        {
            object raw;
            if (!map.TryGetValue(key, out raw)) return fallback;
            if (!(raw is int || raw is long || raw is decimal || raw is double || raw is float))
                throw new InvalidDataException("Numeric atlas property required: " + key);
            double value = Convert.ToDouble(raw, CultureInfo.InvariantCulture);
            if (Double.IsNaN(value) || Double.IsInfinity(value) || value < min || value > max)
                throw new InvalidDataException("Atlas property outside bounds: " + key);
            return value;
        }

        internal void ValidateDesign(double widthDip, double heightDip)
        {
            if (widthDip <= 0 || heightDip <= 0)
                throw new InvalidDataException("Atlas design dimensions required");
            double scaleW = widthDip / SourceWidthPx;
            DesignScaleDip = scaleW;
            double scaleH = heightDip / SourceHeightPx;
            if (Double.IsNaN(scaleW) || Double.IsInfinity(scaleW) ||
                Double.IsNaN(scaleH) || Double.IsInfinity(scaleH) ||
                Math.Abs(scaleW - scaleH) > 0.001)
                throw new InvalidDataException("Atlas design width/height are not isotropic");
        }

        internal void ValidateAsset(int width, int height)
        {
            if (width != SourceWidthPx || height != SourceHeightPx)
                throw new InvalidDataException("Atlas source asset dimensions do not match");
        }

        private static int[][] ReadExclusionRuns(string path)
        {
            string json = GuardedFiles.ReadSmall(path, 4 * 1024 * 1024);
            try
            {
                using (MemoryStream bytes = new MemoryStream(Encoding.UTF8.GetBytes(json)))
                    return new DataContractJsonSerializer(typeof(int[][]))
                        .ReadObject(bytes) as int[][];
            }
            catch (Exception ex)
            {
                throw new InvalidDataException("Atlas exclusion file is not valid int[][] JSON", ex);
            }
        }

        private static int Integer(Dictionary<string, object> map, string key, int min, int max)
        {
            object value;
            if (!map.TryGetValue(key, out value))
                throw new InvalidDataException("Missing numeric atlas property: " + key);
            if (!(value is int || value is long || value is decimal || value is double || value is float))
                throw new InvalidDataException("Numeric atlas property required");
            double number = Convert.ToDouble(value, CultureInfo.InvariantCulture);
            if (Double.IsNaN(number) || Double.IsInfinity(number) ||
                number != Math.Floor(number) || number < min || number > max)
                throw new InvalidDataException("Atlas integer outside bounds");
            return checked((int)number);
        }

        private static string Text(Dictionary<string, object> map, string key)
        {
            object value;
            if (!map.TryGetValue(key, out value) || !(value is string) || String.IsNullOrWhiteSpace((string)value))
                throw new InvalidDataException("String atlas property required: " + key);
            return (string)value;
        }
    }

    internal sealed class AtlasSegment
    {
        internal readonly int Index, SourceX, SourceWidth;
        internal readonly double DestinationX, DestinationWidth, DestinationHeight, ScaleX, ScaleY;

        internal AtlasSegment(int index, int sourceX, int sourceWidth, double destinationX,
            double destinationWidth, double destinationHeight, double scaleX, double scaleY)
        {
            Index = index; SourceX = sourceX; SourceWidth = sourceWidth;
            DestinationX = destinationX; DestinationWidth = destinationWidth;
            DestinationHeight = destinationHeight; ScaleX = scaleX; ScaleY = scaleY;
        }
    }

    internal sealed class AtlasComposition
    {
        internal readonly AtlasSpec Spec;
        internal readonly double Scale, TargetWidthPx, TargetHeightPx;
        internal readonly double FixedBudgetPx, ElasticTotalPx, StretchFactor;
        internal readonly AtlasSegment[] Segments;
        internal readonly string Key;

        private AtlasComposition(AtlasSpec spec, double scale, double targetWidthPx, double targetHeightPx,
            double fixedBudgetPx, double elasticTotalPx, double stretchFactor,
            AtlasSegment[] segments, string key)
        {
            Spec = spec; Scale = scale; TargetWidthPx = targetWidthPx; TargetHeightPx = targetHeightPx;
            FixedBudgetPx = fixedBudgetPx; ElasticTotalPx = elasticTotalPx;
            StretchFactor = stretchFactor; Segments = segments; Key = key;
        }

        internal static AtlasComposition Compute(AtlasSpec spec, double widthDip, double heightDip,
            int cardWidthPx, double dpiScale)
        {
            if (spec == null || cardWidthPx < 1 || dpiScale < 0.5 || dpiScale > 8) return null;
            double scale = widthDip * dpiScale / spec.SourceWidthPx;
            double scaleHeight = heightDip * dpiScale / spec.SourceHeightPx;
            if (Double.IsNaN(scale) || Double.IsInfinity(scale) || scale <= 0 ||
                Double.IsNaN(scaleHeight) || Double.IsInfinity(scaleHeight) ||
                Math.Abs(scale - scaleHeight) > 0.02) return null;

            // s is physical pixels per source pixel. At actualC == cardRightPx*s
            // the source atlas is reproduced at its native design coordinates.
            double targetWidthPx = cardWidthPx + (spec.SourceWidthPx - spec.CardRightPx) * scale;
            double targetHeightPx = spec.SourceHeightPx * scale;
            if (spec.OcclusionMode)
            {
                string fixedKey = spec.Key + ";fixedScale=" + scale.ToString("R", CultureInfo.InvariantCulture) +
                    ";card=" + cardWidthPx.ToString(CultureInfo.InvariantCulture);
                return new AtlasComposition(spec, scale, targetWidthPx, targetHeightPx,
                    0, 0, 1.0, new AtlasSegment[0], fixedKey);
            }
            double fixedBudgetPx = spec.FixedBudgetSourcePx * scale;
            double elasticTotalPx = targetWidthPx - fixedBudgetPx;
            if (targetWidthPx <= 0 || targetHeightPx <= 0 || elasticTotalPx < -0.000001)
                return null;
            double sourceElasticPx = spec.ElasticSourcePx * scale;
            double stretch = elasticTotalPx / sourceElasticPx;
            if (Double.IsNaN(stretch) || Double.IsInfinity(stretch) || stretch < 0) return null;

            double elastic1Width = (spec.EmblemStartPx - spec.LeftFixedEndPx) * scale * stretch;
            double emblemWidth = (spec.EmblemEndPx - spec.EmblemStartPx) * scale;
            double elastic2Width = (spec.RightFixedStartPx - spec.EmblemEndPx) * scale * stretch;
            double x = 0;
            AtlasSegment[] segments = new AtlasSegment[5];
            segments[0] = new AtlasSegment(0, 0, spec.LeftFixedEndPx, x,
                spec.LeftFixedEndPx * scale, targetHeightPx, scale, scale);
            x += segments[0].DestinationWidth;
            segments[1] = new AtlasSegment(1, spec.LeftFixedEndPx,
                spec.EmblemStartPx - spec.LeftFixedEndPx, x, elastic1Width, targetHeightPx,
                scale * stretch, scale);
            x += elastic1Width;
            segments[2] = new AtlasSegment(2, spec.EmblemStartPx,
                spec.EmblemEndPx - spec.EmblemStartPx, x, emblemWidth, targetHeightPx, scale, scale);
            x += emblemWidth;
            segments[3] = new AtlasSegment(3, spec.EmblemEndPx,
                spec.RightFixedStartPx - spec.EmblemEndPx, x, elastic2Width, targetHeightPx,
                scale * stretch, scale);
            x += elastic2Width;
            double rightWidth = (spec.SourceWidthPx - spec.RightFixedStartPx) * scale;
            segments[4] = new AtlasSegment(4, spec.RightFixedStartPx,
                spec.SourceWidthPx - spec.RightFixedStartPx, x, rightWidth, targetHeightPx, scale, scale);
            x += rightWidth;

            StringBuilder key = new StringBuilder();
            key.Append("atlasV1;").Append(spec.Key)
               .Append(";scale=").Append(scale.ToString("R", CultureInfo.InvariantCulture))
               .Append(";total=").Append(targetWidthPx.ToString("R", CultureInfo.InvariantCulture))
               .Append(";height=").Append(targetHeightPx.ToString("R", CultureInfo.InvariantCulture))
               .Append(";fixed=").Append(fixedBudgetPx.ToString("R", CultureInfo.InvariantCulture))
               .Append(";elastic=").Append(elasticTotalPx.ToString("R", CultureInfo.InvariantCulture))
               .Append(";stretch=").Append(stretch.ToString("R", CultureInfo.InvariantCulture))
               .Append(";card=").Append(cardWidthPx.ToString(CultureInfo.InvariantCulture));
            for (int i = 0; i < segments.Length; i++)
            {
                AtlasSegment segment = segments[i];
                key.Append(";seg").Append(i.ToString(CultureInfo.InvariantCulture))
                   .Append('=').Append(segment.SourceX.ToString(CultureInfo.InvariantCulture))
                   .Append(',').Append(segment.SourceWidth.ToString(CultureInfo.InvariantCulture))
                   .Append(',').Append(segment.DestinationX.ToString("R", CultureInfo.InvariantCulture))
                   .Append(',').Append(segment.DestinationWidth.ToString("R", CultureInfo.InvariantCulture))
                   .Append(',').Append(segment.ScaleX.ToString("R", CultureInfo.InvariantCulture));
            }
            return new AtlasComposition(spec, scale, targetWidthPx, targetHeightPx,
                fixedBudgetPx, elasticTotalPx, stretch, segments, key.ToString());
        }
    }

    internal sealed class BandAsset : IDisposable
    {
        internal readonly Bitmap Bitmap;
        internal readonly int Width;
        internal readonly int Height;
        internal readonly string Stamp;
        internal readonly string Path;

        private Bitmap occlusionSourceBackground;
        private Bitmap occlusionSourceForeground;
        private string occlusionSourceKey;
        private Bitmap occlusionScaledBackground;
        private Bitmap occlusionScaledForeground;
        private string occlusionScaledBackgroundKey;
        private string occlusionScaledForegroundKey;
        private bool disposed;

        private static long occlusionSourceBuildCount;
        private static long occlusionScaledBuildCount;
        private static long occlusionCacheHitCount;

        // Read-only counters for targeted offline performance checks.
        internal static long OcclusionSourceBuildCount
        {
            get { return Interlocked.Read(ref occlusionSourceBuildCount); }
        }

        internal static long OcclusionScaledBuildCount
        {
            get { return Interlocked.Read(ref occlusionScaledBuildCount); }
        }

        internal static long OcclusionCacheHitCount
        {
            get { return Interlocked.Read(ref occlusionCacheHitCount); }
        }

        private BandAsset(Bitmap bitmap, string path, string stamp)
        {
            Bitmap = bitmap; Width = bitmap.Width; Height = bitmap.Height; Path = path; Stamp = stamp;
        }

        internal static BandAsset Load(string path)
        {
            string stamp = GuardedFiles.Stamp(path);
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            {
                if (stream.Length > 64L * 1024 * 1024)
                    throw new InvalidDataException("Band image file too large");
                using (Image source = Image.FromStream(stream, false, true))
                {
                    if (source.Width < 1 || source.Height < 1 || source.Width > 16384 ||
                        source.Height > 8192 || (long)source.Width * source.Height > 64L * 1024 * 1024)
                        throw new InvalidDataException("Band image dimensions outside bounds");
                    Bitmap copy = new Bitmap(source.Width, source.Height, PixelFormat.Format32bppPArgb);
                    try
                    {
                        using (Graphics graphics = Graphics.FromImage(copy))
                        {
                            graphics.CompositingMode = CompositingMode.SourceCopy;
                            graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
                            graphics.PixelOffsetMode = PixelOffsetMode.Half;
                            graphics.DrawImageUnscaled(source, 0, 0);
                        }
                        return new BandAsset(copy, path, stamp);
                    }
                    catch
                    {
                        copy.Dispose();
                        throw;
                    }
                }
            }
        }

        // Returns an asset-owned layer. Do not dispose the returned bitmap: the
        // asset replaces it only when the spec key or role scale changes.
        internal Bitmap GetOcclusionLayer(AtlasSpec spec, double scale, bool foreground)
        {
            if (disposed) throw new ObjectDisposedException("BandAsset");
            if (spec == null) throw new ArgumentNullException("spec");
            if (Double.IsNaN(scale) || Double.IsInfinity(scale) || scale <= 0)
                throw new ArgumentOutOfRangeException("scale");
            EnsureOcclusionSource(spec);
            string key = OcclusionLayerKey(spec, scale, foreground);
            if (foreground)
            {
                if (occlusionScaledForeground != null &&
                    String.Equals(occlusionScaledForegroundKey, key, StringComparison.Ordinal))
                {
                    Interlocked.Increment(ref occlusionCacheHitCount);
                    return occlusionScaledForeground;
                }
                Bitmap replacement = CreateScaledOcclusionLayer(occlusionSourceForeground, scale);
                Bitmap previous = occlusionScaledForeground;
                occlusionScaledForeground = replacement;
                occlusionScaledForegroundKey = key;
                if (previous != null) previous.Dispose();
                return replacement;
            }
            if (occlusionScaledBackground != null &&
                String.Equals(occlusionScaledBackgroundKey, key, StringComparison.Ordinal))
            {
                Interlocked.Increment(ref occlusionCacheHitCount);
                return occlusionScaledBackground;
            }
            Bitmap backgroundReplacement = CreateScaledOcclusionLayer(occlusionSourceBackground, scale);
            Bitmap previousBackground = occlusionScaledBackground;
            occlusionScaledBackground = backgroundReplacement;
            occlusionScaledBackgroundKey = key;
            if (previousBackground != null) previousBackground.Dispose();
            return backgroundReplacement;
        }

        internal void PrepareOcclusionSource(AtlasSpec spec)
        {
            if (disposed) throw new ObjectDisposedException("BandAsset");
            EnsureOcclusionSource(spec);
        }

        private void EnsureOcclusionSource(AtlasSpec spec)
        {
            if (occlusionSourceBackground != null && occlusionSourceForeground != null &&
                String.Equals(occlusionSourceKey, spec.Key, StringComparison.Ordinal)) return;
            DisposeOcclusionLayers();
            Bitmap background = null;
            Bitmap foreground = null;
            try
            {
                background = BandRenderer.CreateSourceLayer(this, spec, false);
                Interlocked.Increment(ref occlusionSourceBuildCount);
                foreground = BandRenderer.CreateSourceLayer(this, spec, true);
                Interlocked.Increment(ref occlusionSourceBuildCount);
                occlusionSourceBackground = background;
                occlusionSourceForeground = foreground;
                occlusionSourceKey = spec.Key;
            }
            catch
            {
                if (background != null) background.Dispose();
                if (foreground != null) foreground.Dispose();
                throw;
            }
        }

        private static string OcclusionLayerKey(AtlasSpec spec, double scale, bool foreground)
        {
            return (spec.Key == null ? "none" : spec.Key) + "|" +
                scale.ToString("R", CultureInfo.InvariantCulture) + "|" +
                (foreground ? "foreground" : "background");
        }

        private static Bitmap CreateScaledOcclusionLayer(Bitmap source, double scale)
        {
            if (source == null) throw new ArgumentNullException("source");
            int width = ScaledOcclusionSize(source.Width, scale, 16384);
            int height = ScaledOcclusionSize(source.Height, scale, 8192);
            if ((long)width * height > 64L * 1024 * 1024)
                throw new InvalidOperationException("Scaled occlusion layer is too large");
            Bitmap scaled = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
            try
            {
                using (Graphics graphics = Graphics.FromImage(scaled))
                {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.CompositingQuality = CompositingQuality.HighQuality;
                    graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                    graphics.PixelOffsetMode = PixelOffsetMode.Half;
                    graphics.SmoothingMode = SmoothingMode.HighQuality;
                    graphics.Clear(Color.Transparent);
                    // The canvas is ceil(source * scale), but the source is drawn
                    // to the exact fractional physical size. Ceiling only pads
                    // transparent pixels; it never becomes the scale target.
                    graphics.DrawImage(source,
                        new RectangleF(0, 0, (float)(source.Width * scale), (float)(source.Height * scale)),
                        new RectangleF(0, 0, source.Width, source.Height), GraphicsUnit.Pixel);
                }
                Interlocked.Increment(ref occlusionScaledBuildCount);
                return scaled;
            }
            catch
            {
                scaled.Dispose();
                throw;
            }
        }

        private static int ScaledOcclusionSize(int sourcePixels, double scale, int maximum)
        {
            double value = Math.Ceiling(sourcePixels * scale);
            if (Double.IsNaN(value) || Double.IsInfinity(value) || value < 1 || value > maximum)
                throw new InvalidOperationException("Scaled occlusion layer dimensions outside bounds");
            return checked((int)value);
        }

        private void DisposeOcclusionLayers()
        {
            if (occlusionScaledBackground != null) occlusionScaledBackground.Dispose();
            if (occlusionScaledForeground != null) occlusionScaledForeground.Dispose();
            if (occlusionSourceBackground != null) occlusionSourceBackground.Dispose();
            if (occlusionSourceForeground != null) occlusionSourceForeground.Dispose();
            occlusionScaledBackground = null;
            occlusionScaledForeground = null;
            occlusionScaledBackgroundKey = null;
            occlusionScaledForegroundKey = null;
            occlusionSourceBackground = null;
            occlusionSourceForeground = null;
            occlusionSourceKey = null;
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            DisposeOcclusionLayers();
            Bitmap.Dispose();
        }
    }

    internal sealed class Placement
    {
        internal int Width, Height, HostX, HostY, CardX, CardY, CardWidth;
        internal uint Pid, HwndDpi;
        internal double Scale;
        internal long Observed;
        internal double[] ItemEdgesDip;
        private static readonly Regex Pattern = new Regex(
            @"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[p(?<pid>\d+):t\d+\] candidate-d2d place source=[a-z-]+ scale=(?<s>\d+(?:\.\d+)?) hwnd_dpi=(?<dpi>\d+)\b[^\r\n]*?size_px=\((?<w>\d+),(?<h>\d+)\) host=\((?<x>-?\d+),(?<y>-?\d+)\)[^\r\n]*?card_px=\((?<cx>-?\d+),(?<cy>-?\d+) (?<cw>\d+)w\)",
            RegexOptions.CultureInvariant);

        internal static Placement Parse(string line, long now)
        {
            Match match = Pattern.Match(line);
            if (!match.Success) return null;
            try
            {
                Placement placement = new Placement();
                placement.Pid = UInt32.Parse(match.Groups["pid"].Value, CultureInfo.InvariantCulture);
                placement.HwndDpi = UInt32.Parse(match.Groups["dpi"].Value, CultureInfo.InvariantCulture);
                placement.Scale = Double.Parse(match.Groups["s"].Value, CultureInfo.InvariantCulture);
                placement.Width = Int32.Parse(match.Groups["w"].Value, CultureInfo.InvariantCulture);
                placement.Height = Int32.Parse(match.Groups["h"].Value, CultureInfo.InvariantCulture);
                placement.HostX = Int32.Parse(match.Groups["x"].Value, CultureInfo.InvariantCulture);
                placement.HostY = Int32.Parse(match.Groups["y"].Value, CultureInfo.InvariantCulture);
                placement.CardX = Int32.Parse(match.Groups["cx"].Value, CultureInfo.InvariantCulture);
                placement.CardY = Int32.Parse(match.Groups["cy"].Value, CultureInfo.InvariantCulture);
                placement.CardWidth = Int32.Parse(match.Groups["cw"].Value, CultureInfo.InvariantCulture);
                placement.Observed = now;
                if (placement.Pid == 0 || placement.HwndDpi < 48 || placement.HwndDpi > 768 ||
                    placement.Scale < 0.5 || placement.Scale > 8 || placement.Width < 1 ||
                    placement.Width > 16384 || placement.Height < 1 || placement.Height > 8192 ||
                    placement.CardWidth < 1 ||
                    (long)placement.CardX - placement.HostX < 0 ||
                    (long)placement.CardY - placement.HostY < 0 ||
                    (long)placement.CardX - placement.HostX + placement.CardWidth > placement.Width ||
                    (long)placement.CardY - placement.HostY >= placement.Height) return null;
                Match edges = Regex.Match(line, @" item_edges_dip=\[(?<values>[^\]\r\n]*)\]", RegexOptions.CultureInvariant);
                if (edges.Success)
                {
                    string values = edges.Groups["values"].Value;
                    string[] parts = values.Length == 0 ? new string[0] : values.Split(',');
                    if (parts.Length > 20) return null;
                    placement.ItemEdgesDip = new double[parts.Length];
                    double previous = 0;
                    for (int i = 0; i < parts.Length; i++)
                    {
                        double value;
                        if (!Double.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out value) ||
                            Double.IsNaN(value) || Double.IsInfinity(value) || value <= previous ||
                            value >= placement.CardWidth / placement.Scale) return null;
                        placement.ItemEdgesDip[i] = value;
                        previous = value;
                    }
                }
                return placement;
            }
            catch (FormatException) { return null; }
            catch (OverflowException) { return null; }
        }

        internal bool TryCardFromLiveRect(Native.RECT live, out int cardX, out int cardY, out string reason)
        {
            cardX = cardY = 0;
            if (live.Right - live.Left != Width || live.Bottom - live.Top != Height)
            {
                reason = "target-size-mismatch";
                return false;
            }
            int relativeX = CardX - HostX;
            int relativeY = CardY - HostY;
            long x = (long)live.Left + relativeX;
            long y = (long)live.Top + relativeY;
            if (x < Int32.MinValue || x > Int32.MaxValue || y < Int32.MinValue || y > Int32.MaxValue)
            {
                reason = "card-coordinate-overflow";
                return false;
            }
            cardX = (int)x;
            cardY = (int)y;
            reason = null;
            return true;
        }
    }

    internal sealed class DiagnosticTail
    {
        private readonly string path;
        private long offset = -1, creation;
        private Decoder decoder;
        private readonly StringBuilder partial = new StringBuilder();
        internal Placement Latest;

        internal DiagnosticTail(string path) { this.path = path; }

        internal void PrimeAtEnd()
        {
            FileInfo info = new FileInfo(path);
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            {
                byte[] bom = new byte[3];
                int length = stream.Read(bom, 0, 3);
                Encoding encoding = length >= 2 && bom[0] == 255 && bom[1] == 254 ? Encoding.Unicode :
                    length >= 2 && bom[0] == 254 && bom[1] == 255 ? Encoding.BigEndianUnicode :
                    new UTF8Encoding(false, true);
                decoder = encoding.GetDecoder();
                creation = info.CreationTimeUtc.Ticks;
                offset = stream.Length;
                partial.Length = 0;
                Latest = null;
            }
        }

        internal void Poll(long now)
        {
            FileInfo info = new FileInfo(path);
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            {
                if (offset < 0 || creation != info.CreationTimeUtc.Ticks || stream.Length < offset)
                {
                    byte[] bom = new byte[3];
                    int length = stream.Read(bom, 0, 3);
                    Encoding encoding = length >= 2 && bom[0] == 255 && bom[1] == 254 ? Encoding.Unicode :
                        length >= 2 && bom[0] == 254 && bom[1] == 255 ? Encoding.BigEndianUnicode :
                        new UTF8Encoding(false, true);
                    decoder = encoding.GetDecoder();
                    creation = info.CreationTimeUtc.Ticks;
                    offset = stream.Length;
                    partial.Length = 0;
                    Latest = null;
                    return;
                }
                if (stream.Length - offset > 65536)
                {
                    offset = stream.Length;
                    partial.Length = 0;
                    decoder.Reset();
                    Latest = null;
                    return;
                }
                stream.Position = offset;
                byte[] bytes = new byte[(int)(stream.Length - offset)];
                int read = stream.Read(bytes, 0, bytes.Length);
                offset += read;
                char[] characters = new char[65538];
                int count = decoder.GetChars(bytes, 0, read, characters, 0, false);
                for (int i = 0; i < count; i++)
                {
                    char character = characters[i];
                    if (character == '\n')
                    {
                        Placement placement = Placement.Parse(partial.ToString(), now);
                        if (placement != null) Latest = placement;
                        partial.Length = 0;
                    }
                    else if (character != '\r')
                    {
                        if (partial.Length >= 4096) { partial.Length = 0; Latest = null; }
                        partial.Append(character);
                    }
                }
            }
        }

        internal void Invalidate() { Latest = null; }
    }

    internal sealed class BandLayout
    {
        internal readonly int CardScreenX, CardScreenY, CardWidthPx;
        internal readonly int OriginX, OriginY, Width, Height;
        internal readonly int CardLocalX, CardLocalY;
        internal readonly double Scale;
        internal readonly double FullWidthPx, FullHeightPx, VisibleWidthPx;
        internal readonly FretSpec Frets;
        internal readonly bool FretsActive;
        internal readonly double[] ItemEdgesDip;
        internal readonly AtlasComposition Atlas;
        internal readonly string LocalKey;

        private BandLayout(int cardX, int cardY, int cardWidth, int originX, int originY,
            int width, int height, int cardLocalX, int cardLocalY, double scale,
            double fullWidth, double fullHeight, double visibleWidth,
            FretSpec frets, bool fretsActive, AtlasComposition atlas, string localKey, double[] itemEdgesDip = null)
        {
            CardScreenX = cardX; CardScreenY = cardY; CardWidthPx = cardWidth;
            OriginX = originX; OriginY = originY; Width = width; Height = height;
            CardLocalX = cardLocalX; CardLocalY = cardLocalY;
            Scale = scale; FullWidthPx = fullWidth; FullHeightPx = fullHeight;
            VisibleWidthPx = visibleWidth; Frets = frets; FretsActive = fretsActive;
            Atlas = atlas; LocalKey = localKey;
            ItemEdgesDip = itemEdgesDip == null ? null : (double[])itemEdgesDip.Clone();
        }

        internal bool AtlasMode
        {
            get { return Atlas != null; }
        }

        internal double ActualCardWidthDip
        {
            get { return (double)CardWidthPx / Scale; }
        }

        internal double CutoffFromCardPx
        {
            get { return VisibleWidthPx; }
        }

        internal bool EndsAtOrBeforeScreenCutoff(double cutOffsetDip)
        {
            double cutoff = CardScreenX + CardWidthPx + cutOffsetDip * Scale;
            return (double)OriginX + Width <= cutoff + 0.000001;
        }

        internal static BandLayout Compute(OverlayConfig config, int cardScreenX, int cardScreenY,
            int cardWidthPx, double scale)
        {
            return Compute(config, cardScreenX, cardScreenY, cardWidthPx, scale, null);
        }

        internal static BandLayout Compute(OverlayConfig config, int cardScreenX, int cardScreenY,
            int cardWidthPx, double scale, double[] itemEdgesDip)
        {
            if (config == null || cardWidthPx < 1 || scale < 0.5 || scale > 8)
                return null;
            if (itemEdgesDip != null)
                for (int i = 0; i < itemEdgesDip.Length; i++)
                    if (Double.IsNaN(itemEdgesDip[i]) || Double.IsInfinity(itemEdgesDip[i]) ||
                        itemEdgesDip[i] <= (i == 0 ? 0 : itemEdgesDip[i-1]) ||
                        itemEdgesDip[i] >= cardWidthPx / scale) return null;
            bool fretsActive = config.Frets != null && (config.Frets.FollowNativeItems ?
                itemEdgesDip != null && itemEdgesDip.Length > 0 :
                Math.Abs((double)cardWidthPx / scale - config.Frets.CardWidthDip) <= config.Frets.ToleranceDip);
            string fretsKey = config.Frets == null ? "frets=absent" :
                "frets=" + config.Frets.Key + ";native=" + config.Frets.FollowNativeItems + ";active=" + (fretsActive ? "1" : "0");
            if (itemEdgesDip != null)
                foreach (double edge in itemEdgesDip) fretsKey += ";edge=" + edge.ToString("R", CultureInfo.InvariantCulture);

            long originX = (long)cardScreenX + (long)Math.Floor(config.OffsetXDip * scale);
            long originY = (long)cardScreenY + (long)Math.Floor(config.OffsetYDip * scale);
            if (originX < Int32.MinValue || originX > Int32.MaxValue ||
                originY < Int32.MinValue || originY > Int32.MaxValue)
                return null;
            int cardLocalX = checked(cardScreenX - (int)originX);
            int cardLocalY = checked(cardScreenY - (int)originY);

            if (config.Atlas != null)
            {
                AtlasComposition atlas = AtlasComposition.Compute(config.Atlas,
                    config.WidthDip, config.HeightDip, cardWidthPx, scale);
                if (atlas == null) return null;
                int atlasWidth = (int)Math.Ceiling(atlas.TargetWidthPx);
                int atlasHeight = (int)Math.Ceiling(atlas.TargetHeightPx);
                if (atlasWidth < 1 || atlasHeight < 1 ||
                    atlasWidth > 16384 || atlasHeight > 8192) return null;
                double designWidth = config.Atlas.SourceWidthPx * atlas.Scale;
                string atlasKey = "atlas|" +
                    config.WidthDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                    config.HeightDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                    config.OffsetXDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                    config.OffsetYDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                    scale.ToString("R", CultureInfo.InvariantCulture) + "|" +
                    cardWidthPx.ToString(CultureInfo.InvariantCulture) + "|" +
                    fretsKey + "|" + atlas.Key;
                return new BandLayout(cardScreenX, cardScreenY, cardWidthPx,
                    (int)originX, (int)originY, atlasWidth, atlasHeight,
                    cardLocalX, cardLocalY, scale, designWidth, atlas.TargetHeightPx,
                    atlas.TargetWidthPx, config.Frets, fretsActive, atlas, atlasKey, itemEdgesDip);
            }

            double fullWidth = config.WidthDip * scale;
            double fullHeight = config.HeightDip * scale;
            if (Double.IsNaN(fullWidth) || Double.IsInfinity(fullWidth) ||
                Double.IsNaN(fullHeight) || Double.IsInfinity(fullHeight) ||
                fullWidth <= 0 || fullHeight <= 0) return null;

            // Legacy band mode: unchanged rectangular tail crop.
            double visibleWidth = Math.Min(fullWidth,
                cardWidthPx + (config.CutOffsetXDip - config.OffsetXDip) * scale);
            if (Double.IsNaN(visibleWidth) || Double.IsInfinity(visibleWidth) || visibleWidth < 1)
                return null;
            int bitmapWidth = (int)Math.Floor(visibleWidth);
            int bitmapHeight = (int)Math.Ceiling(fullHeight);
            if (bitmapWidth < 1 || bitmapHeight < 1 || bitmapWidth > 16384 || bitmapHeight > 8192)
                return null;

            string key = config.WidthDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                config.HeightDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                config.OffsetXDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                config.OffsetYDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                config.CutOffsetXDip.ToString("R", CultureInfo.InvariantCulture) + "|" +
                scale.ToString("R", CultureInfo.InvariantCulture) + "|" +
                cardWidthPx.ToString(CultureInfo.InvariantCulture) + "|" + fretsKey;
            return new BandLayout(cardScreenX, cardScreenY, cardWidthPx, (int)originX, (int)originY,
                bitmapWidth, bitmapHeight, cardLocalX, cardLocalY, scale, fullWidth, fullHeight,
                visibleWidth, config.Frets, fretsActive, null, key, itemEdgesDip);
        }
    }

    internal sealed class RenderedBand : IDisposable
    {
        internal readonly Bitmap Bitmap;
        internal readonly string CacheKey;
        internal readonly double ContainScale;
        internal readonly RectangleF ArtDestination;

        internal RenderedBand(Bitmap bitmap, string cacheKey, double containScale, RectangleF artDestination)
        {
            Bitmap = bitmap; CacheKey = cacheKey; ContainScale = containScale; ArtDestination = artDestination;
        }

        public void Dispose() { Bitmap.Dispose(); }
    }

    internal static class BandRenderer
    {
        internal static RenderedBand Render(BandAsset asset, BandLayout layout)
        {
            if (asset == null || layout == null) throw new ArgumentNullException("band");
            if (layout.AtlasMode) return RenderAtlas(asset, layout);
            // Uniform contain, centered in the declared full band box. The
            // destination bitmap starts at the band origin and has already been
            // cut at the trusted card boundary. No portrait mask participates.
            double fit = Math.Min(layout.FullWidthPx / asset.Width, layout.FullHeightPx / asset.Height);
            if (fit <= 0 || Double.IsNaN(fit) || Double.IsInfinity(fit))
                throw new InvalidOperationException("Invalid contain fit");
            double contentWidth = asset.Width * fit;
            double contentHeight = asset.Height * fit;
            double contentX = (layout.FullWidthPx - contentWidth) * 0.5;
            double contentY = (layout.FullHeightPx - contentHeight) * 0.5;

            double destLeft = Math.Max(0.0, contentX);
            double destTop = Math.Max(0.0, contentY);
            double destRight = Math.Min((double)layout.Width, contentX + contentWidth);
            double destBottom = Math.Min((double)layout.Height, contentY + contentHeight);
            double srcLeft = Math.Max(0.0, (destLeft - contentX) / fit);
            double srcTop = Math.Max(0.0, (destTop - contentY) / fit);
            double srcRight = Math.Min((double)asset.Width, (destRight - contentX) / fit);
            double srcBottom = Math.Min((double)asset.Height, (destBottom - contentY) / fit);

            Bitmap bitmap = new Bitmap(layout.Width, layout.Height, PixelFormat.Format32bppPArgb);
            try
            {
                using (Graphics graphics = Graphics.FromImage(bitmap))
                {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.CompositingQuality = CompositingQuality.HighQuality;
                    graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                    graphics.PixelOffsetMode = PixelOffsetMode.Half;
                    graphics.SmoothingMode = SmoothingMode.HighQuality;
                    graphics.Clear(Color.Transparent);
                    if (destRight > destLeft && destBottom > destTop &&
                        srcRight > srcLeft && srcBottom > srcTop)
                    {
                        RectangleF source = new RectangleF((float)srcLeft, (float)srcTop,
                            (float)(srcRight - srcLeft), (float)(srcBottom - srcTop));
                        RectangleF destination = new RectangleF((float)destLeft, (float)destTop,
                            (float)(destRight - destLeft), (float)(destBottom - destTop));
                        // Direct source-rectangle crop: the band tail beyond the
                        // board is never composited into this bitmap.
                        graphics.DrawImage(asset.Bitmap, destination, source, GraphicsUnit.Pixel);
                    }
                }
                DrawFrets(bitmap, layout);
                string key = asset.Stamp + "|" + layout.LocalKey;
                return new RenderedBand(bitmap, key, fit,
                    new RectangleF((float)contentX, (float)contentY, (float)contentWidth, (float)contentHeight));
            }
            catch
            {
                bitmap.Dispose();
                throw;
            }
        }

        private static RenderedBand RenderAtlas(BandAsset asset, BandLayout layout)
        {
            AtlasComposition atlas = layout.Atlas;
            if (atlas == null) throw new InvalidOperationException("Atlas composition required");
            atlas.Spec.ValidateAsset(asset.Width, asset.Height);
            if (atlas.Spec.OcclusionMode) return RenderOcclusion(asset, layout);
            Bitmap bitmap = new Bitmap(layout.Width, layout.Height, PixelFormat.Format32bppPArgb);
            try
            {
                using (Graphics graphics = Graphics.FromImage(bitmap))
                {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.CompositingQuality = CompositingQuality.HighQuality;
                    graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                    graphics.PixelOffsetMode = PixelOffsetMode.Half;
                    graphics.SmoothingMode = SmoothingMode.HighQuality;
                    graphics.Clear(Color.Transparent);
                    for (int i = 0; i < atlas.Segments.Length; i++)
                    {
                        AtlasSegment segment = atlas.Segments[i];
                        if (segment.DestinationWidth <= 0 || segment.SourceWidth <= 0) continue;
                        using (Region excluded = BuildExclusionRegion(segment, atlas))
                        {
                            // The source PNG is never edited. Runtime runs are
                            // mapped into this segment's destination clip only.
                            if (excluded != null) graphics.ExcludeClip(excluded);
                            RectangleF source = new RectangleF(segment.SourceX, 0,
                                segment.SourceWidth, atlas.Spec.SourceHeightPx);
                            RectangleF destination = new RectangleF(
                                (float)segment.DestinationX, 0,
                                (float)segment.DestinationWidth, (float)atlas.TargetHeightPx);
                            graphics.DrawImage(asset.Bitmap, destination, source, GraphicsUnit.Pixel);
                            graphics.ResetClip();
                        }
                        // Restore only the tiny white star rim hidden by the
                        // baked-in chibi shoe. No orange or pink is repainted.
                        using (Region restored = BuildRunsRegion(segment, atlas, atlas.Spec.RestoreWhiteRuns))
                        using (Brush white = new SolidBrush(Color.FromArgb(255, 253, 253, 253)))
                            if (restored != null) graphics.FillRegion(white, restored);
                    }
                }
                // Frets stay after every atlas segment, still under native text.
                DrawFrets(bitmap, layout);
                return new RenderedBand(bitmap, asset.Stamp + "|" + layout.LocalKey, atlas.Scale,
                    new RectangleF(0, 0, (float)atlas.TargetWidthPx, (float)atlas.TargetHeightPx));
            }
            catch
            {
                bitmap.Dispose();
                throw;
            }
        }

        private static RenderedBand RenderOcclusion(BandAsset asset, BandLayout layout)
        {
            AtlasComposition atlas = layout.Atlas;
            double fullWidth = atlas.Spec.SourceWidthPx * atlas.Scale;
            double portraitScale = atlas.Scale * atlas.Spec.ForegroundScale;
            double portraitShift = atlas.TargetWidthPx - atlas.Spec.SourceWidthPx * portraitScale;
            bool separatePortrait = atlas.Spec.PortraitRuns.Length > 0;
            double shift = separatePortrait ? atlas.TargetWidthPx - fullWidth : portraitShift;
            double foregroundY = (atlas.Scale - portraitScale) * atlas.Spec.ForegroundAnchorYPx +
                atlas.Spec.ForegroundOffsetYDip * layout.Scale;
            double cutoff = portraitShift + atlas.Spec.OcclusionCutPx * portraitScale;
            if (separatePortrait) foregroundY = 0;
            Bitmap bitmap = new Bitmap(layout.Width, layout.Height, PixelFormat.Format32bppPArgb);
            try
            {
                // Mask at source resolution before interpolation. A destination
                // clip alone lets bilinear sampling leak hidden sprite edges.
                // Both layers are asset-owned and reused across card widths.
                Bitmap background = asset.GetOcclusionLayer(atlas.Spec, atlas.Scale, false);
                Bitmap foreground = asset.GetOcclusionLayer(
                    atlas.Spec, separatePortrait ? atlas.Scale : portraitScale, true);
                using (Graphics graphics = Graphics.FromImage(bitmap))
                {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                    graphics.PixelOffsetMode = PixelOffsetMode.Half;
                    graphics.Clear(Color.Transparent);
                    graphics.SetClip(new RectangleF(0, 0, (float)Math.Max(0, cutoff), (float)atlas.TargetHeightPx));
                    // The scaled cache is already at physical resolution; 1:1
                    // compositing must not rescale it a second time.
                    graphics.DrawImageUnscaled(background, 0, 0);
                    graphics.ResetClip();
                    graphics.CompositingMode = CompositingMode.SourceOver;
                    // Keep DrawImage (not Unscaled) so a negative/fractional
                    // shift retains the previous subpixel placement behavior.
                    graphics.DrawImage(foreground,
                        new RectangleF((float)shift, (float)foregroundY, foreground.Width, foreground.Height),
                        new RectangleF(0, 0, foreground.Width, foreground.Height), GraphicsUnit.Pixel);
                }
                DrawFrets(bitmap, layout);
                return new RenderedBand(bitmap, asset.Stamp + "|" + layout.LocalKey, atlas.Scale,
                    new RectangleF(0, 0, (float)atlas.TargetWidthPx, (float)atlas.TargetHeightPx));
            }
            catch { bitmap.Dispose(); throw; }
        }

        internal static Bitmap CreateSourceLayer(BandAsset asset, AtlasSpec spec, bool foreground)
        {
            Bitmap layer = new Bitmap(asset.Width, asset.Height, PixelFormat.Format32bppPArgb);
            try
            {
                using (Graphics graphics = Graphics.FromImage(layer))
                using (Region kept = SourceRunsRegion(spec.ForegroundRuns, foreground ? 0 : spec.BackgroundRemovalMarginPx))
                using (Region removed = SourceRunsRegion(spec.ExcludeRuns))
                using (Region restored = SourceRunsRegion(spec.RestoreWhiteRuns))
                using (Brush transparent = new SolidBrush(Color.Transparent))
                using (Brush white = new SolidBrush(Color.FromArgb(255, 253, 253, 253)))
                {
                    graphics.CompositingMode = CompositingMode.SourceCopy;
                    graphics.Clear(Color.Transparent);
                    if (foreground) graphics.SetClip(kept, CombineMode.Replace);
                    if (foreground && spec.PortraitRuns.Length > 0)
                        using (Region portraitClip = SourceRunsRegion(spec.PortraitRuns))
                            graphics.ExcludeClip(portraitClip);
                    graphics.DrawImageUnscaled(asset.Bitmap, 0, 0);
                    graphics.ResetClip();
                    if (foreground && spec.PortraitRuns.Length > 0)
                    {
                        using (Bitmap portrait = new Bitmap(asset.Width, asset.Height, PixelFormat.Format32bppPArgb))
                        {
                            using (Graphics portraitGraphics = Graphics.FromImage(portrait))
                            using (Region portraitClip = SourceRunsRegion(spec.PortraitRuns))
                            {
                                portraitGraphics.CompositingMode = CompositingMode.SourceCopy;
                                portraitGraphics.Clear(Color.Transparent);
                                portraitGraphics.SetClip(portraitClip, CombineMode.Replace);
                                portraitGraphics.DrawImageUnscaled(asset.Bitmap, 0, 0);
                            }
                            double q = spec.ForegroundScale;
                            graphics.CompositingMode = CompositingMode.SourceOver;
                            graphics.InterpolationMode = InterpolationMode.HighQualityBilinear;
                            graphics.PixelOffsetMode = PixelOffsetMode.Half;
                            graphics.DrawImage(portrait, new RectangleF((float)((1-q)*asset.Width),
                                (float)((1-q)*spec.ForegroundAnchorYPx + spec.ForegroundOffsetYDip/spec.DesignScaleDip),
                                (float)(asset.Width*q),(float)(asset.Height*q)),
                                new RectangleF(0,0,asset.Width,asset.Height),GraphicsUnit.Pixel);
                        }
                    }
                    if (!foreground)
                    {
                        graphics.FillRegion(transparent, kept);
                        graphics.FillRegion(transparent, removed);
                        if (spec.BackgroundRepairSourceX >= 0)
                        {
                            // Restore the neck hidden in the flattened source
                            // sticker before moving/scaling that sticker. Copy
                            // existing clean ribbon pixels at 1:1 only inside
                            // the old foreground holes, never stretch the band.
                            using (Region repair = kept.Clone())
                            {
                            repair.Union(removed);
                            graphics.SetClip(repair, CombineMode.Replace);
                            graphics.IntersectClip(new Rectangle(spec.BackgroundRepairTargetX,
                                spec.BackgroundRepairTop, asset.Width - spec.BackgroundRepairTargetX,
                                spec.BackgroundRepairBottom - spec.BackgroundRepairTop));
                            graphics.DrawImageUnscaled(asset.Bitmap,
                                spec.BackgroundRepairTargetX - spec.BackgroundRepairSourceX, 0);
                            graphics.ResetClip();
                            }
                        }
                        graphics.FillRegion(white, restored);
                    }
                }
                return layer;
            }
            catch { layer.Dispose(); throw; }
        }

        private static Region SourceRunsRegion(int[][] runs, int margin = 0)
        {
            Region region = new Region();
            region.MakeEmpty();
            if (runs != null)
                foreach (int[] run in runs)
                    region.Union(new Rectangle(run[1] - margin, run[0] - margin,
                        run[2] - run[1] + margin * 2, 1 + margin * 2));
            return region;
        }

        internal static Region BuildExclusionRegion(AtlasSegment segment, AtlasComposition atlas)
        {
            return BuildRunsRegion(segment, atlas, atlas == null ? null : atlas.Spec.ExcludeRuns);
        }

        private static Region BuildRunsRegion(AtlasSegment segment, AtlasComposition atlas, int[][] runs)
        {
            if (segment == null || atlas == null || runs == null || runs.Length == 0) return null;
            int segmentEnd = segment.SourceX + segment.SourceWidth;
            Region region = new Region();
            region.MakeEmpty();
            bool any = false;
            foreach (int[] run in runs)
            {
                int xStart = Math.Max(run[1], segment.SourceX);
                int xEnd = Math.Min(run[2], segmentEnd);
                if (xEnd <= xStart) continue;
                double destinationX = segment.DestinationX + (xStart - segment.SourceX) * segment.ScaleX;
                double destinationWidth = (xEnd - xStart) * segment.ScaleX;
                double destinationY = run[0] * segment.ScaleY;
                if (destinationWidth <= 0 || segment.ScaleY <= 0) continue;
                region.Union(new RectangleF((float)destinationX, (float)destinationY,
                    (float)destinationWidth, (float)segment.ScaleY));
                any = true;
            }
            if (!any)
            {
                region.Dispose();
                return null;
            }
            return region;
        }

        private static void DrawFrets(Bitmap bitmap, BandLayout layout)
        {
            if (bitmap == null || layout == null || !layout.FretsActive || layout.Frets == null) return;
            FretSpec frets = layout.Frets;
            int lineCount = frets.FollowNativeItems ? layout.ItemEdgesDip.Length : frets.Count;
            if (lineCount <= 0) return;
            double top = layout.CardLocalY + frets.TopDip * layout.Scale;
            double bottom = layout.CardLocalY + frets.BottomDip * layout.Scale;
            double lineWidthPx = frets.LineWidthDip * layout.Scale;
            double centerGapPx = frets.LineGapDip * layout.Scale;
            if (lineWidthPx <= 0 || centerGapPx <= 0 || bottom <= top) return;

            int yStart = (int)Math.Floor(top);
            int yEnd = (int)Math.Ceiling(bottom);
            if (yStart < 0) yStart = 0;
            if (yEnd > bitmap.Height) yEnd = bitmap.Height;
            if (yStart >= yEnd) return;

            int colorR = (frets.ColorRgb >> 16) & 0xFF;
            int colorG = (frets.ColorRgb >> 8) & 0xFF;
            int colorB = frets.ColorRgb & 0xFF;
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
                ImageLockMode.ReadWrite, PixelFormat.Format32bppPArgb);
            try
            {
                for (int i = 0; i < lineCount; i++)
                {
                    double boundary = layout.CardLocalX +
                        (frets.FollowNativeItems ? layout.ItemEdgesDip[i] :
                            frets.StartXDip + frets.PitchDip * (i + 1)) * layout.Scale;
                    for (int side = -1; side <= 1; side += 2)
                    {
                        double center = boundary + side * centerGapPx * 0.5;
                        double left = center - lineWidthPx * 0.5;
                        double right = center + lineWidthPx * 0.5;
                        int xStart = (int)Math.Floor(left);
                        int xEnd = (int)Math.Ceiling(right);
                        if (xStart < 0) xStart = 0;
                        if (xEnd > bitmap.Width) xEnd = bitmap.Width;
                        if (xStart >= xEnd) continue;

                        for (int x = xStart; x < xEnd; x++)
                        {
                            double coverageX = Math.Min(x + 1.0, right) - Math.Max((double)x, left);
                            if (coverageX <= 0) continue;
                            for (int y = yStart; y < yEnd; y++)
                            {
                                double coverageY = Math.Min(y + 1.0, bottom) - Math.Max((double)y, top);
                                if (coverageY <= 0) continue;
                                int alpha = (int)Math.Round(coverageX * coverageY * 255.0,
                                    MidpointRounding.AwayFromZero);
                                if (alpha <= 0) continue;

                                IntPtr pixel = IntPtr.Add(data.Scan0, y * data.Stride + x * 4);
                                int oldBgra = Marshal.ReadInt32(pixel);
                                int oldB = oldBgra & 0xFF;
                                int oldG = (oldBgra >> 8) & 0xFF;
                                int oldR = (oldBgra >> 16) & 0xFF;
                                int oldA = (oldBgra >> 24) & 0xFF;
                                int inverse = 255 - alpha;
                                int sourceB = (colorB * alpha + 127) / 255;
                                int sourceG = (colorG * alpha + 127) / 255;
                                int sourceR = (colorR * alpha + 127) / 255;
                                int outB = sourceB + (oldB * inverse + 127) / 255;
                                int outG = sourceG + (oldG * inverse + 127) / 255;
                                int outR = sourceR + (oldR * inverse + 127) / 255;
                                int outA = alpha + (oldA * inverse + 127) / 255;
                                if (outA > 255) outA = 255;
                                if (outB > outA) outB = outA;
                                if (outG > outA) outG = outA;
                                if (outR > outA) outR = outA;
                                Marshal.WriteInt32(pixel,
                                    outB | (outG << 8) | (outR << 16) | (outA << 24));
                            }
                        }
                    }
                }
            }
            finally { bitmap.UnlockBits(data); }
        }
    }

    internal static class Native
    {
        internal const string ServerPath = @"C:\Program Files\MetasequoiaIME\server\MetasequoiaImeServer.exe";
        internal const string CandidateClass = "metasequoiaime_windows";
        internal const string CandidateCaption = "metaseuqoiaimecandwnd";
        internal const int GwlExStyle = -20;
        internal const long WsExLayered = 0x00080000L;
        internal const long WsExTransparent = 0x00000020L;
        internal const long WsExNoActivate = 0x08000000L;
        internal const long WsExToolWindow = 0x00000080L;
        internal const long WsExTopmost = 0x00000008L;
        internal const long WsExNoRedirectionBitmap = 0x00200000L;
        internal const uint SwpNoActivate = 0x0010;
        internal const uint SwpNoOwnerZOrder = 0x0200;
        internal const uint SwpShowWindow = 0x0040;
        internal const int SwHide = 0;
        internal const uint UlwAlpha = 2;
        internal const int AcSrcOver = 0;
        internal const int AcSrcAlpha = 1;
        internal const int DwmwaCloaked = 14;
        internal const uint WmUserApplyImeConfig = 0x0400 + 116;

        internal delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
        internal delegate IntPtr WndProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential)]
        internal struct RECT { internal int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct POINT { internal int X, Y; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct SIZE { internal int Cx, Cy; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct BLENDFUNCTION
        {
            internal byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct BITMAPINFOHEADER
        {
            internal uint biSize;
            internal int biWidth, biHeight;
            internal ushort biPlanes, biBitCount;
            internal uint biCompression, biSizeImage;
            internal int biXPelsPerMeter, biYPelsPerMeter;
            internal uint biClrUsed, biClrImportant;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct RGBQUAD { internal byte Blue, Green, Red, Reserved; }
        [StructLayout(LayoutKind.Sequential)]
        internal struct BITMAPINFO { internal BITMAPINFOHEADER Header; internal RGBQUAD Colors; }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct WNDCLASSEX
        {
            internal uint cbSize, style;
            internal IntPtr lpfnWndProc;
            internal int cbClsExtra, cbWndExtra;
            internal IntPtr hInstance, hIcon, hCursor, hbrBackground;
            [MarshalAs(UnmanagedType.LPWStr)] internal string lpszMenuName;
            [MarshalAs(UnmanagedType.LPWStr)] internal string lpszClassName;
            internal IntPtr hIconSm;
        }
        [StructLayout(LayoutKind.Sequential)]
        internal struct MSG
        {
            internal IntPtr hwnd;
            internal uint message;
            internal IntPtr wParam, lParam;
            internal uint time;
            internal POINT pt;
        }

        [DllImport("user32.dll")] internal static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
        [DllImport("user32.dll")] internal static extern bool IsWindow(IntPtr hwnd);
        [DllImport("user32.dll")] internal static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] internal static extern bool IsIconic(IntPtr hwnd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] internal static extern int GetClassName(IntPtr hwnd, StringBuilder text, int capacity);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] internal static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int capacity);
        [DllImport("user32.dll")] internal static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
        [DllImport("user32.dll")] internal static extern uint GetDpiForWindow(IntPtr hwnd);
        [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW", SetLastError = true)]
        internal static extern IntPtr GetWindowLongPtr(IntPtr hwnd, int index);
        [DllImport("user32.dll", SetLastError = true)] internal static extern IntPtr SendMessageTimeout(
            IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam, uint flags, uint timeout, out UIntPtr result);
        [DllImport("user32.dll")] internal static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);
        [DllImport("dwmapi.dll")] internal static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool SetWindowPos(
            IntPtr hwnd, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool UpdateLayeredWindow(
            IntPtr hwnd, IntPtr destinationDc, ref POINT destination, ref SIZE size, IntPtr sourceDc,
            ref POINT source, int colorKey, ref BLENDFUNCTION blend, uint flags);
        [DllImport("user32.dll")] internal static extern bool ShowWindow(IntPtr hwnd, int command);
        [DllImport("user32.dll", SetLastError = true)] internal static extern IntPtr GetTopWindow(IntPtr hwnd);
        [DllImport("user32.dll", SetLastError = true)] internal static extern IntPtr GetWindow(IntPtr hwnd, uint command);
        [DllImport("user32.dll", SetLastError = true)] internal static extern IntPtr GetDC(IntPtr hwnd);
        [DllImport("user32.dll")] internal static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);
        [DllImport("gdi32.dll", SetLastError = true)] internal static extern IntPtr CreateCompatibleDC(IntPtr dc);
        [DllImport("gdi32.dll", SetLastError = true)] internal static extern bool DeleteDC(IntPtr dc);
        [DllImport("gdi32.dll", SetLastError = true)] internal static extern IntPtr CreateDIBSection(
            IntPtr dc, ref BITMAPINFO info, uint usage, out IntPtr bits, IntPtr section, uint offset);
        [DllImport("gdi32.dll", SetLastError = true)] internal static extern IntPtr SelectObject(IntPtr dc, IntPtr value);
        [DllImport("gdi32.dll", SetLastError = true)] internal static extern bool DeleteObject(IntPtr value);
        [DllImport("kernel32.dll", SetLastError = true)] internal static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder path, ref int length);
        [DllImport("kernel32.dll")] internal static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll")] internal static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] internal static extern IntPtr GetModuleHandle(string module);
        [DllImport("kernel32.dll")] internal static extern void SetLastError(uint error);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool SetProcessDpiAwarenessContext(IntPtr value);
        [DllImport("user32.dll")] internal static extern IntPtr GetThreadDpiAwarenessContext();
        [DllImport("user32.dll")] internal static extern int GetAwarenessFromDpiAwarenessContext(IntPtr value);
        [DllImport("user32.dll", SetLastError = true)] internal static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern ushort RegisterClassEx(ref WNDCLASSEX windowClass);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern IntPtr CreateWindowEx(uint exStyle, string className, string windowName,
            uint style, int x, int y, int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr parameter);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool DestroyWindow(IntPtr hwnd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool UnregisterClass(string className, IntPtr instance);
        [DllImport("user32.dll")] internal static extern IntPtr DefWindowProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", SetLastError = true)] internal static extern uint SetTimer(IntPtr hwnd, uint id, uint interval, IntPtr timerProc);
        [DllImport("user32.dll", SetLastError = true)] internal static extern bool KillTimer(IntPtr hwnd, uint id);
        [DllImport("user32.dll", SetLastError = true)] internal static extern int GetMessage(out MSG message, IntPtr hwnd, uint filterMin, uint filterMax);
        [DllImport("user32.dll")] internal static extern bool TranslateMessage(ref MSG message);
        [DllImport("user32.dll")] internal static extern IntPtr DispatchMessage(ref MSG message);
        [DllImport("user32.dll")] internal static extern void PostQuitMessage(int exitCode);

        internal static bool ClassMatches(IntPtr hwnd)
        {
            StringBuilder name = new StringBuilder(256);
            if (GetClassName(hwnd, name, name.Capacity) <= 0 ||
                !String.Equals(name.ToString(), CandidateClass, StringComparison.Ordinal)) return false;
            StringBuilder caption = new StringBuilder(64);
            return GetWindowText(hwnd, caption, caption.Capacity) > 0 &&
                String.Equals(caption.ToString(), CandidateCaption, StringComparison.Ordinal);
        }

        internal static bool ProcessMatches(IntPtr process)
        {
            StringBuilder path = new StringBuilder(32768);
            int capacity = path.Capacity;
            return QueryFullProcessImageName(process, 0, path, ref capacity) &&
                String.Equals(path.ToString(), ServerPath, StringComparison.OrdinalIgnoreCase);
        }

        internal static bool IsOwnWindowBelow(IntPtr target, IntPtr own)
        {
            IntPtr current = GetTopWindow(IntPtr.Zero);
            bool targetSeen = false;
            int guard = 0;
            while (current != IntPtr.Zero && guard++ < 100000)
            {
                if (current == target) targetSeen = true;
                else if (current == own) return targetSeen;
                current = GetWindow(current, 2); // GW_HWNDNEXT
            }
            return false;
        }

        internal static bool UpdateLayered(IntPtr hwnd, Bitmap bitmap, int x, int y, out int error)
        {
            error = 0;
            if (hwnd == IntPtr.Zero || bitmap == null) { error = 87; return false; }
            IntPtr screen = IntPtr.Zero, memory = IntPtr.Zero, dib = IntPtr.Zero, old = IntPtr.Zero;
            try
            {
                screen = GetDC(IntPtr.Zero);
                if (screen == IntPtr.Zero) { error = Marshal.GetLastWin32Error(); return false; }
                memory = CreateCompatibleDC(screen);
                if (memory == IntPtr.Zero) { error = Marshal.GetLastWin32Error(); return false; }

                BITMAPINFO info = new BITMAPINFO();
                info.Header.biSize = (uint)Marshal.SizeOf(typeof(BITMAPINFOHEADER));
                info.Header.biWidth = bitmap.Width;
                info.Header.biHeight = bitmap.Height;
                info.Header.biPlanes = 1;
                info.Header.biBitCount = 32;
                info.Header.biCompression = 0; // BI_RGB
                IntPtr bits;
                dib = CreateDIBSection(memory, ref info, 0, out bits, IntPtr.Zero, 0);
                if (dib == IntPtr.Zero || bits == IntPtr.Zero) { error = Marshal.GetLastWin32Error(); return false; }
                old = SelectObject(memory, dib);
                if (old == IntPtr.Zero || old == new IntPtr(-1)) { error = Marshal.GetLastWin32Error(); return false; }

                BitmapData data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
                    ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    int sourceStride = data.Stride;
                    int rowBytes = checked(bitmap.Width * 4);
                    byte[] row = new byte[rowBytes];
                    for (int rowIndex = 0; rowIndex < bitmap.Height; rowIndex++)
                    {
                        IntPtr source = IntPtr.Add(data.Scan0, rowIndex * sourceStride);
                        Marshal.Copy(source, row, 0, rowBytes);
                        // The source bitmap is 32bpp premultiplied. Clamp against
                        // rounding so ULW always receives valid premultiplied BGRA.
                        for (int p = 0; p < rowBytes; p += 4)
                        {
                            int alpha = row[p + 3];
                            if (row[p] > alpha) row[p] = (byte)alpha;
                            if (row[p + 1] > alpha) row[p + 1] = (byte)alpha;
                            if (row[p + 2] > alpha) row[p + 2] = (byte)alpha;
                        }
                        IntPtr destination = IntPtr.Add(bits, (bitmap.Height - 1 - rowIndex) * rowBytes);
                        Marshal.Copy(row, 0, destination, rowBytes);
                    }
                }
                finally { bitmap.UnlockBits(data); }

                POINT destinationPoint = new POINT { X = x, Y = y };
                SIZE size = new SIZE { Cx = bitmap.Width, Cy = bitmap.Height };
                POINT sourcePoint = new POINT { X = 0, Y = 0 };
                BLENDFUNCTION blend = new BLENDFUNCTION
                {
                    BlendOp = AcSrcOver, BlendFlags = 0, SourceConstantAlpha = 255, AlphaFormat = AcSrcAlpha
                };
                SetLastError(0);
                bool updated = UpdateLayeredWindow(hwnd, screen, ref destinationPoint, ref size,
                    memory, ref sourcePoint, 0, ref blend, UlwAlpha);
                if (!updated) { error = Marshal.GetLastWin32Error(); return false; }
                return true;
            }
            catch (OverflowException) { error = 87; return false; }
            finally
            {
                if (old != IntPtr.Zero && old != new IntPtr(-1) && memory != IntPtr.Zero)
                    SelectObject(memory, old);
                if (dib != IntPtr.Zero) DeleteObject(dib);
                if (memory != IntPtr.Zero) DeleteDC(memory);
                if (screen != IntPtr.Zero) ReleaseDC(IntPtr.Zero, screen);
            }
        }
    }

    internal sealed class LogWriter
    {
        private const long MaxBytes = 262144;
        private readonly object gate = new object();
        internal readonly string Path;

        internal LogWriter(string dataDir)
        {
            Path = System.IO.Path.Combine(dataDir, "kasumi-band-overlay.log");
        }

        internal void Write(string message)
        {
            try
            {
                lock (gate)
                {
                    FileInfo info = new FileInfo(Path);
                    if (info.Exists && info.Length > MaxBytes)
                        File.WriteAllText(Path, "", new UTF8Encoding(false));
                    File.AppendAllText(Path,
                        DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) + " " + message + Environment.NewLine,
                        new UTF8Encoding(false));
                }
            }
            catch { /* Logging is bounded and must never alter the IME. */ }
        }
    }

    internal static class OldClipperGuard
    {
        internal const string StopEventName = @"Local\KasumiCandidateClipper-stop-v1";

        internal static void StopAndWait(LogWriter log)
        {
            bool hadProcesses = AnyRunning();
            bool eventOpened = false;
            string signalError = null;
            try
            {
                using (EventWaitHandle signal = EventWaitHandle.OpenExisting(StopEventName,
                    EventWaitHandleRights.Modify))
                {
                    eventOpened = true;
                    bool set;
                    try { set = signal.Set(); }
                    catch (Exception ex) { signalError = ex.GetType().Name; set = false; }
                    if (!set && signalError == null)
                    {
                        int error = Marshal.GetLastWin32Error();
                        signalError = "win32:" + error.ToString(CultureInfo.InvariantCulture);
                    }
                }
            }
            catch (WaitHandleCannotBeOpenedException) { signalError = "event-not-found"; }
            catch (UnauthorizedAccessException) { signalError = "event-access-denied"; }
            catch (Exception ex) { signalError = ex.GetType().Name; }

            if (hadProcesses && !eventOpened)
                throw new InvalidOperationException("Old clipper is running but its stop event cannot be opened: " + signalError);
            if (hadProcesses && !String.IsNullOrEmpty(signalError))
                throw new InvalidOperationException("Old clipper stop signal failed: " + signalError);

            Stopwatch wait = Stopwatch.StartNew();
            if (hadProcesses)
            {
                while (AnyRunning() && wait.ElapsedMilliseconds < 5000)
                    Thread.Sleep(100);
            }
            bool stillRunning = AnyRunning();
            log.Write("oldClipperStopRequested=" + (hadProcesses ? "true" : "false") +
                " runningBefore=" + (hadProcesses ? "true" : "false") +
                " eventAvailable=" + (eventOpened ? "true" : "false") +
                " waitedMs=" + wait.ElapsedMilliseconds.ToString(CultureInfo.InvariantCulture));
            if (stillRunning)
            {
                log.Write("oldClipperStillRunning=true waitedMs=5000");
                throw new InvalidOperationException("Old clipper is still running after 5 seconds; new overlay will not start");
            }
        }

        private static bool AnyRunning()
        {
            Process[] processes = null;
            try
            {
                processes = Process.GetProcessesByName("KasumiCandidateClipper");
                return processes.Length > 0;
            }
            catch { return true; } // fail closed if the process list is unavailable
            finally
            {
                if (processes != null) foreach (Process process in processes) process.Dispose();
            }
        }
    }

    internal static class ReplaceRunningGuard
    {
        internal static void SignalAndWait(LogWriter log)
        {
            string currentPath;
            int currentPid, currentSession;
            using (Process current = Process.GetCurrentProcess())
            {
                currentPid = current.Id;
                currentSession = current.SessionId;
                try { currentPath = current.MainModule.FileName; }
                catch (Exception ex)
                {
                    throw new InvalidOperationException("Cannot resolve current overlay path", ex);
                }
            }

            List<int> before = FindFamilyProcesses(currentPath, currentPid, currentSession);
            if (before.Count == 0)
            {
                log.Write("replaceRunningRequested=true oldFamilyProcesses=0");
                return;
            }

            bool signalSent = false;
            string signalError = null;
            try
            {
                using (EventWaitHandle signal = EventWaitHandle.OpenExisting(
                    OverlayContext.StopEventName, EventWaitHandleRights.Modify))
                {
                    try { signalSent = signal.Set(); }
                    catch (Exception ex) { signalError = ex.GetType().Name; }
                }
            }
            catch (WaitHandleCannotBeOpenedException) { signalError = "event-not-found"; }
            catch (UnauthorizedAccessException) { signalError = "event-access-denied"; }
            catch (Exception ex) { signalError = ex.GetType().Name; }

            if (!signalSent)
            {
                log.Write("replaceRunningFailed=true reason=stop-signal-failed detail=" +
                    (signalError == null ? "unknown" : signalError));
                throw new InvalidOperationException("Cannot signal the running overlay to exit");
            }

            Stopwatch wait = Stopwatch.StartNew();
            int remaining = FindFamilyProcesses(currentPath, currentPid, currentSession).Count;
            while (remaining > 0 && wait.ElapsedMilliseconds < 5000)
            {
                Thread.Sleep(100);
                remaining = FindFamilyProcesses(currentPath, currentPid, currentSession).Count;
            }
            log.Write("replaceRunningRequested=true oldFamilyProcesses=" +
                before.Count.ToString(CultureInfo.InvariantCulture) +
                " stopSignalSent=true waitedMs=" +
                wait.ElapsedMilliseconds.ToString(CultureInfo.InvariantCulture) +
                " remaining=" + remaining.ToString(CultureInfo.InvariantCulture));
            if (remaining > 0)
            {
                log.Write("replaceRunningFailed=true reason=still-running waitedMs=5000");
                throw new InvalidOperationException("Previous family overlay process did not exit within 5 seconds");
            }
        }

        internal static bool IsSameFamilyPath(string currentPath, string candidatePath)
        {
            try
            {
                string current = Path.GetFullPath(currentPath);
                string candidate = Path.GetFullPath(candidatePath);
                if (!String.Equals(Path.GetFileName(current), Path.GetFileName(candidate),
                    StringComparison.OrdinalIgnoreCase)) return false;
                string currentDir = Path.GetDirectoryName(current);
                string candidateDir = Path.GetDirectoryName(candidate);
                if (String.Equals(currentDir, candidateDir, StringComparison.OrdinalIgnoreCase))
                    return true;

                DirectoryInfo parent = Directory.GetParent(currentDir);
                if (parent == null || !IsVersionDirectory(Path.GetFileName(currentDir))) return false;
                string familyRoot = parent.FullName;
                if (String.Equals(candidateDir, familyRoot, StringComparison.OrdinalIgnoreCase))
                    return true;
                DirectoryInfo candidateParent = Directory.GetParent(candidateDir);
                return candidateParent != null &&
                    String.Equals(candidateParent.FullName, familyRoot, StringComparison.OrdinalIgnoreCase) &&
                    IsVersionDirectory(Path.GetFileName(candidateDir));
            }
            catch { return false; }
        }

        private static List<int> FindFamilyProcesses(string currentPath, int currentPid, int currentSession)
        {
            List<int> family = new List<int>();
            Process[] processes = Process.GetProcessesByName("KasumiBandOverlay");
            try
            {
                foreach (Process process in processes)
                {
                    if (process.Id == currentPid) continue;
                    int session;
                    try { session = process.SessionId; }
                    catch (InvalidOperationException) { continue; }
                    if (session != currentSession) continue;
                    string path;
                    try { path = QueryExecutablePath(process.Id); }
                    catch (Exception ex)
                    {
                        bool alive;
                        try { alive = !process.HasExited; }
                        catch { alive = true; }
                        if (!alive) continue;
                        throw new InvalidOperationException("Cannot verify replacement process identity", ex);
                    }
                    if (IsSameFamilyPath(currentPath, path)) family.Add(process.Id);
                }
            }
            finally
            {
                foreach (Process process in processes)
                {
                    try { process.Dispose(); } catch { }
                }
            }
            return family;
        }

        private static string QueryExecutablePath(int pid)
        {
            IntPtr process = Native.OpenProcess(0x00101000, false, (uint)pid);
            if (process == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "OpenProcess for replacement check failed");
            try
            {
                StringBuilder path = new StringBuilder(32768);
                int length = path.Capacity;
                if (!Native.QueryFullProcessImageName(process, 0, path, ref length))
                    throw new Win32Exception(Marshal.GetLastWin32Error(),
                        "QueryFullProcessImageName for replacement check failed");
                return path.ToString();
            }
            finally { Native.CloseHandle(process); }
        }

        private static bool IsVersionDirectory(string name)
        {
            return Regex.IsMatch(name, @"^v\d+(?:\.\d+){1,3}$",
                RegexOptions.CultureInvariant | RegexOptions.IgnoreCase);
        }
    }

    internal sealed class OverlayContext : IDisposable
    {
        internal const string StopEventName = @"Local\KasumiBandOverlay-stop-v1";
        internal const string WindowClass = "KasumiBandOverlayWindowV1";
        private const uint TimerId = 0xB4D;
        private const uint WmNcHitTest = 0x0084;
        private const uint WmMouseActivate = 0x0021;
        private const uint WmEraseBackground = 0x0014;
        private const uint WmTimer = 0x0113;
        private const uint WmClose = 0x0010;
        private const uint WmEndSession = 0x0016;
        private const uint WmDestroy = 0x0002;

        private readonly Options options;
        private readonly LogWriter log;
        private readonly DiagnosticTail tail;
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly EventWaitHandle stopEvent;
        private readonly HashSet<string> loggedErrors = new HashSet<string>();
        private readonly Native.WndProc wndProcDelegate;
        private readonly IntPtr instance;
        private OverlayConfig config;
        private BandAsset asset;
        private RenderedBand rendered;
        private IntPtr hwnd;
        private ushort classAtom;
        private bool timerSet, overlayVisible, closing, disposed;
        private bool enabled;
        private IntPtr shownTarget;
        private int shownX, shownY, shownWidth, shownHeight;
        private long lastConfigCheck = -1000;
        private string lastHiddenReason;
        private static OverlayContext messageTarget;

        internal OverlayContext(Options options, OverlayConfig config, BandAsset asset, LogWriter log)
        {
            this.options = options; this.config = config; this.asset = asset; this.log = log;
            tail = new DiagnosticTail(options.DiagnosticLog);
            bool created;
            stopEvent = new EventWaitHandle(false, EventResetMode.ManualReset, StopEventName, out created);
            if (!created) stopEvent.Reset();
            instance = Native.GetModuleHandle(null);
            if (instance == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "GetModuleHandle failed");

            messageTarget = this;
            wndProcDelegate = new Native.WndProc(WindowProcedure);
            Native.WNDCLASSEX windowClass = new Native.WNDCLASSEX();
            windowClass.cbSize = (uint)Marshal.SizeOf(typeof(Native.WNDCLASSEX));
            windowClass.lpfnWndProc = Marshal.GetFunctionPointerForDelegate(wndProcDelegate);
            windowClass.hInstance = instance;
            windowClass.lpszClassName = WindowClass;
            Native.SetLastError(0);
            classAtom = Native.RegisterClassEx(ref windowClass);
            if (classAtom == 0)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "RegisterClassEx failed");

            uint exStyle = (uint)(Native.WsExLayered | Native.WsExTransparent | Native.WsExNoActivate |
                Native.WsExToolWindow | Native.WsExTopmost);
            Native.SetLastError(0);
            hwnd = Native.CreateWindowEx(exStyle, WindowClass, "", 0x80000000u, 0, 0, 1, 1,
                IntPtr.Zero, IntPtr.Zero, instance, IntPtr.Zero);
            if (hwnd == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateWindowEx failed");
            long actualStyle = Native.GetWindowLongPtr(hwnd, Native.GwlExStyle).ToInt64();
            long required = Native.WsExLayered | Native.WsExTransparent | Native.WsExNoActivate |
                Native.WsExToolWindow | Native.WsExTopmost;
            if ((actualStyle & required) != required)
            {
                Hide("overlay-style-missing");
                throw new InvalidOperationException("Overlay window is missing required extended styles");
            }
            AppDomain.CurrentDomain.ProcessExit += OnProcessExit;
            PrimeTailAndMaybeReload();
            Native.SetLastError(0);
            uint timer = Native.SetTimer(hwnd, TimerId, 16, IntPtr.Zero);
            if (timer == 0)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetTimer failed");
            timerSet = true;
            lastConfigCheck = clock.ElapsedMilliseconds;
        }

        private void PrimeTailAndMaybeReload()
        {
            try
            {
                tail.PrimeAtEnd();
                log.Write("diagnosticPrimed=true");
            }
            catch (Exception ex)
            {
                log.Write("diagnosticPrimed=false error=" + ex.GetType().Name);
            }

            bool selected = false;
            try { selected = SkinSelection.IsSelected(options.DataDir); }
            catch (Exception ex) { LogErrorOnce("selection-" + ex.GetType().Name, "selection-check-failed", 0); }
            enabled = selected;
            if (!options.ReloadSkin)
            {
                log.Write("reloadRequested=false reason=option-not-set visualVerified=false");
                return;
            }
            if (!selected)
            {
                log.Write("reloadRequested=false reason=skin-not-selected visualVerified=false");
                return;
            }
            // The tail is already at EOF before this force-reload request, so
            // only placements produced after startup can become trusted.
            ReloadRequest.Send(log);
        }

        internal void Run()
        {
            try
            {
                while (true)
                {
                    Native.MSG message;
                    Native.SetLastError(0);
                    int result = Native.GetMessage(out message, IntPtr.Zero, 0, 0);
                    if (result == -1)
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "GetMessage failed");
                    if (result == 0) break;
                    Native.TranslateMessage(ref message);
                    Native.DispatchMessage(ref message);
                }
            }
            finally { Dispose(); }
        }

        private static IntPtr WindowProcedure(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam)
        {
            OverlayContext context = messageTarget;
            if (context == null) return Native.DefWindowProc(hwnd, message, wParam, lParam);
            try { return context.HandleMessage(hwnd, message, wParam, lParam); }
            catch (Exception ex)
            {
                context.LogErrorOnce("wndproc-" + ex.GetType().Name, "wndproc-failed", 0);
                context.Hide("window-message-failed");
                return Native.DefWindowProc(hwnd, message, wParam, lParam);
            }
        }

        private IntPtr HandleMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam)
        {
            if (message == WmNcHitTest) return new IntPtr(-1); // HTTRANSPARENT
            if (message == WmMouseActivate) return new IntPtr(3); // MA_NOACTIVATE
            if (message == WmEraseBackground) return new IntPtr(1);
            if (message == WmTimer)
            {
                if (wParam.ToInt64() == TimerId) Tick();
                return IntPtr.Zero;
            }
            if (message == WmClose) { RequestExit("window-close"); return IntPtr.Zero; }
            if (message == WmEndSession)
            {
                if (wParam != IntPtr.Zero) RequestExit("session-end");
                return IntPtr.Zero;
            }
            if (message == WmDestroy)
            {
                Native.PostQuitMessage(0);
                return IntPtr.Zero;
            }
            return Native.DefWindowProc(hwnd, message, wParam, lParam);
        }

        private void Tick()
        {
            if (closing) return;
            try
            {
                if (stopEvent.WaitOne(0))
                {
                    RequestExit("stop-event");
                    return;
                }
                long now = clock.ElapsedMilliseconds;
                CheckConfiguration(now);
                if (!enabled)
                {
                    Hide("skin-not-selected");
                    return;
                }
                try { tail.Poll(now); }
                catch (Exception ex)
                {
                    tail.Invalidate();
                    LogErrorOnce("diagnostic-" + ex.GetType().Name, "diagnostic-read-failed", 0);
                    Hide("diagnostic-read-failed");
                    return;
                }
                Placement placement = tail.Latest;
                if (placement == null)
                {
                    // No fresh, matching diagnostic means no card-width guess.
                    Hide("no-fresh-placement");
                    return;
                }

                TargetInfo target;
                string reason;
                if (!TryFindTarget(placement, out target, out reason))
                {
                    Hide(reason);
                    return;
                }
                int cardX, cardY;
                if (!placement.TryCardFromLiveRect(target.Rect, out cardX, out cardY, out reason))
                {
                    Hide(reason);
                    return;
                }
                BandLayout layout = BandLayout.Compute(config, cardX, cardY, placement.CardWidth, placement.Scale, placement.ItemEdgesDip);
                if (layout == null) { Hide("layout-empty"); return; }
                string key = asset.Stamp + "|" + layout.LocalKey;
                RenderedBand fresh = null;
                if (rendered == null || !String.Equals(rendered.CacheKey, key, StringComparison.Ordinal))
                    fresh = BandRenderer.Render(asset, layout);

                bool geometryMoved = target.Hwnd != shownTarget || layout.OriginX != shownX ||
                    layout.OriginY != shownY || layout.Width != shownWidth || layout.Height != shownHeight;
                bool contentUpdated = fresh != null;
                bool needsShow = !overlayVisible;

                if (contentUpdated || needsShow)
                {
                    RenderedBand toApply = fresh ?? rendered;
                    if (toApply == null) { if (fresh != null) fresh.Dispose(); Hide("render-empty"); return; }
                    int error;
                    if (!Native.UpdateLayered(hwnd, toApply.Bitmap, layout.OriginX, layout.OriginY, out error))
                    {
                        if (fresh != null) fresh.Dispose();
                        Hide("update-layered-window-failed:" + error.ToString(CultureInfo.InvariantCulture));
                        return;
                    }
                    if (!PlaceOwnWindow(target.Hwnd, layout, needsShow, out error))
                    {
                        if (fresh != null) fresh.Dispose();
                        Hide("set-window-pos-failed:" + error.ToString(CultureInfo.InvariantCulture));
                        return;
                    }
                    if (!Native.IsOwnWindowBelow(target.Hwnd, hwnd))
                    {
                        if (fresh != null) fresh.Dispose();
                        Hide("own-window-not-below-target");
                        return;
                    }
                    if (contentUpdated)
                    {
                        RenderedBand old = rendered;
                        rendered = fresh;
                        fresh = null;
                        if (old != null) old.Dispose();
                    }
                    MarkShown(target.Hwnd, layout);
                    log.Write("band_applied hwnd=" + target.Hwnd.ToInt64().ToString(CultureInfo.InvariantCulture) +
                        " pid=" + placement.Pid.ToString(CultureInfo.InvariantCulture) +
                        " cardWidth=" + placement.CardWidth.ToString(CultureInfo.InvariantCulture) +
                        " cut=" + layout.VisibleWidthPx.ToString("0.###", CultureInfo.InvariantCulture) +
                        " bitmapWidth=" + layout.Width.ToString(CultureInfo.InvariantCulture) +
                        " ownWindowBelowTarget=true" +
                        " atlasMode=" + (layout.AtlasMode ? "true" : "false") +
                        (layout.AtlasMode ? " stretchFactor=" +
                            layout.Atlas.StretchFactor.ToString("R", CultureInfo.InvariantCulture) : ""));
                }
                else if (geometryMoved)
                {
                    int error;
                    if (!PlaceOwnWindow(target.Hwnd, layout, false, out error))
                    {
                        Hide("set-window-pos-failed:" + error.ToString(CultureInfo.InvariantCulture));
                        return;
                    }
                    if (!Native.IsOwnWindowBelow(target.Hwnd, hwnd))
                    {
                        Hide("own-window-not-below-target");
                        return;
                    }
                    MarkShown(target.Hwnd, layout);
                    // Position-only updates deliberately do not rebuild or
                    // re-render the cached band. The cache key has no screen
                    // coordinates.
                }
            }
            catch (Exception ex)
            {
                Hide("observer-error");
                LogErrorOnce("observer-" + ex.GetType().Name, "observer-failed", 0);
            }
            finally
            {
                if (!closing && hwnd == IntPtr.Zero) Hide("window-missing");
            }
        }

        private bool PlaceOwnWindow(IntPtr target, BandLayout layout, bool show, out int error)
        {
            error = 0;
            uint flags = Native.SwpNoActivate | Native.SwpNoOwnerZOrder;
            if (show) flags |= Native.SwpShowWindow;
            Native.SetLastError(0);
            // This is the only SetWindowPos call in the helper. It targets the
            // helper's own hwnd, with the identity-checked official candidate
            // host as insertAfter, so this window sits behind the native text.
            if (!Native.SetWindowPos(hwnd, target, layout.OriginX, layout.OriginY,
                layout.Width, layout.Height, flags))
            {
                error = Marshal.GetLastWin32Error();
                return false;
            }
            overlayVisible = true;
            return true;
        }

        private void MarkShown(IntPtr target, BandLayout layout)
        {
            shownTarget = target;
            shownX = layout.OriginX; shownY = layout.OriginY;
            shownWidth = layout.Width; shownHeight = layout.Height;
            lastHiddenReason = null;
        }

        private void TryFindCandidates(Placement placement, out TargetInfo target, out string reason)
        {
            target = new TargetInfo();
            reason = "no-official-window";
            string localReason = "no-official-window";
            int fullyMatching = 0;
            TargetInfo found = new TargetInfo();
            Native.EnumWindows(delegate(IntPtr hwnd, IntPtr parameter)
            {
                if (!Native.ClassMatches(hwnd)) return true;
                if (!Native.IsWindowVisible(hwnd)) { localReason = "target-not-visible"; return true; }
                uint pid;
                if (Native.GetWindowThreadProcessId(hwnd, out pid) == 0) { localReason = "target-pid-unavailable"; return true; }
                if (pid != placement.Pid) { localReason = "target-pid-mismatch"; return true; }
                long exStyle = Native.GetWindowLongPtr(hwnd, Native.GwlExStyle).ToInt64();
                if ((exStyle & Native.WsExNoRedirectionBitmap) == 0) { localReason = "target-style-mismatch"; return true; }
                if ((exStyle & Native.WsExTopmost) == 0) { localReason = "target-not-topmost"; return true; }
                uint dpi = Native.GetDpiForWindow(hwnd);
                if (dpi != placement.HwndDpi) { localReason = "target-dpi-mismatch"; return true; }
                Native.RECT rect;
                if (!Native.GetWindowRect(hwnd, out rect)) { localReason = "target-rect-read-failed"; return true; }
                if (rect.Right - rect.Left != placement.Width || rect.Bottom - rect.Top != placement.Height)
                { localReason = "target-size-mismatch"; return true; }
                if (Native.IsIconic(hwnd) || rect.Left <= -32000 || rect.Top <= -32000 ||
                    rect.Right <= -32000 || rect.Bottom <= -32000)
                { localReason = "target-offscreen"; return true; }
                if (Native.MonitorFromWindow(hwnd, 0) == IntPtr.Zero)
                { localReason = "target-offscreen"; return true; }

                IntPtr process = Native.OpenProcess(0x00101000, false, pid);
                if (process == IntPtr.Zero) { localReason = "process-query-denied"; return true; }
                try
                {
                    if (!Native.ProcessMatches(process)) { localReason = "target-process-mismatch"; return true; }
                    if (Native.WaitForSingleObject(process, 0) != 258) { localReason = "target-process-exited"; return true; }
                }
                finally { Native.CloseHandle(process); }

                int cloaked;
                int dwmResult = Native.DwmGetWindowAttribute(hwnd, Native.DwmwaCloaked, out cloaked, 4);
                if (dwmResult != 0) { localReason = "target-dwm-query-failed"; return true; }
                if (cloaked != 0) { localReason = "target-dwm-cloaked"; return true; }

                Native.RECT client;
                Native.POINT origin = new Native.POINT();
                if (!Native.GetClientRect(hwnd, out client) || client.Right <= 0 || client.Bottom <= 0 ||
                    !Native.ClientToScreen(hwnd, ref origin) || origin.X < rect.Left || origin.Y < rect.Top ||
                    (long)origin.X + client.Right > rect.Right || (long)origin.Y + client.Bottom > rect.Bottom)
                { localReason = "target-client-mismatch"; return true; }

                fullyMatching++;
                if (fullyMatching == 1)
                {
                    found = new TargetInfo { Hwnd = hwnd, Pid = pid, Dpi = dpi, Rect = rect };
                }
                return true;
            }, IntPtr.Zero);

            if (fullyMatching == 0) { target = new TargetInfo(); reason = localReason; return; }
            if (fullyMatching > 1)
            {
                reason = "ambiguous-official-windows";
                target = new TargetInfo();
                return;
            }
            target = found;
            reason = null;
        }

        private bool TryFindTarget(Placement placement, out TargetInfo target, out string reason)
        {
            TryFindCandidates(placement, out target, out reason);
            return reason == null && target.Hwnd != IntPtr.Zero;
        }

        private void CheckConfiguration(long now)
        {
            if (now - lastConfigCheck < 500) return;
            lastConfigCheck = now;
            bool selected;
            try { selected = SkinSelection.IsSelected(options.DataDir); }
            catch (Exception ex)
            {
                if (enabled) tail.Invalidate();
                enabled = false;
                Hide("skin-config-invalid");
                LogErrorOnce("selection-" + ex.GetType().Name, "selection-check-failed", 0);
                return;
            }
            if (!selected)
            {
                if (enabled) tail.Invalidate();
                enabled = false;
                Hide("skin-not-selected");
                return;
            }
            if (!enabled)
            {
                enabled = true;
                lastHiddenReason = null;
                try { tail.PrimeAtEnd(); }
                catch { tail.Invalidate(); }
            }
            if (config == null || !config.FilesUnchanged())
            {
                try
                {
                    OverlayConfig replacement = OverlayConfig.Load(options.SkinDir);
                    BandAsset replacementAsset = BandAsset.Load(replacement.ImagePath);
                    try { replacement.ValidateAsset(replacementAsset); }
                    catch
                    {
                        replacementAsset.Dispose();
                        throw;
                    }
                    BandAsset oldAsset = asset;
                    RenderedBand oldRendered = rendered;
                    config = replacement;
                    asset = replacementAsset;
                    rendered = null;
                    if (oldRendered != null) oldRendered.Dispose();
                    if (oldAsset != null) oldAsset.Dispose();
                    shownTarget = IntPtr.Zero;
                    Hide("skin-config-reloaded");
                    tail.Invalidate();
                    try { tail.PrimeAtEnd(); } catch { tail.Invalidate(); }
                }
                catch (Exception ex)
                {
                    config = null;
                    if (asset != null) { asset.Dispose(); asset = null; }
                    if (rendered != null) { rendered.Dispose(); rendered = null; }
                    enabled = false;
                    tail.Invalidate();
                    Hide("skin-config-invalid");
                    LogErrorOnce("config-" + ex.GetType().Name, "config-reload-failed", 0);
                }
            }
        }

        private void Hide(string reason)
        {
            if (hwnd != IntPtr.Zero && overlayVisible)
            {
                Native.ShowWindow(hwnd, Native.SwHide);
                overlayVisible = false;
            }
            shownTarget = IntPtr.Zero;
            if (!String.Equals(lastHiddenReason, reason, StringComparison.Ordinal))
            {
                lastHiddenReason = reason;
                log.Write("hidden reason=" + reason);
            }
        }

        private void LogErrorOnce(string key, string message, int win32)
        {
            if (!loggedErrors.Add(key + ":" + win32.ToString(CultureInfo.InvariantCulture))) return;
            log.Write("error=" + message + " detail=" + key +
                " win32=" + win32.ToString(CultureInfo.InvariantCulture));
        }

        private void RequestExit(string reason)
        {
            if (closing) return;
            closing = true;
            if (timerSet && hwnd != IntPtr.Zero)
            {
                if (!Native.KillTimer(hwnd, TimerId))
                    LogErrorOnce("kill-timer", "kill-timer-failed", Marshal.GetLastWin32Error());
                timerSet = false;
            }
            Hide("exit-" + reason);
            Native.PostQuitMessage(0);
        }

        private void OnProcessExit(object sender, EventArgs args) { Dispose(); }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            closing = true;
            try
            {
                if (timerSet && hwnd != IntPtr.Zero)
                {
                    if (!Native.KillTimer(hwnd, TimerId))
                        log.Write("error=kill-timer-failed win32=" + Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture));
                    timerSet = false;
                }
                if (hwnd != IntPtr.Zero)
                {
                    Native.ShowWindow(hwnd, Native.SwHide);
                    overlayVisible = false;
                    Native.SetLastError(0);
                    bool destroyed = Native.DestroyWindow(hwnd);
                    if (!destroyed)
                        log.Write("error=destroy-window-failed win32=" + Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture));
                    hwnd = IntPtr.Zero;
                }
                if (classAtom != 0 && instance != IntPtr.Zero)
                {
                    Native.SetLastError(0);
                    if (!Native.UnregisterClass(WindowClass, instance))
                        log.Write("error=unregister-class-failed win32=" + Marshal.GetLastWin32Error().ToString(CultureInfo.InvariantCulture));
                    classAtom = 0;
                }
                if (rendered != null) { rendered.Dispose(); rendered = null; }
                if (asset != null) { asset.Dispose(); asset = null; }
                if (stopEvent != null) stopEvent.Dispose();
                messageTarget = null;
                AppDomain.CurrentDomain.ProcessExit -= OnProcessExit;
                log.Write("exit reason=normal shutdown");
            }
            catch (Exception ex)
            {
                log.Write("error=dispose-failed detail=" + ex.GetType().Name);
            }
        }

        internal sealed class TargetInfo
        {
            internal IntPtr Hwnd;
            internal uint Pid, Dpi;
            internal Native.RECT Rect;
        }
    }

    internal static class ReloadRequest
    {
        internal static void Send(LogWriter log)
        {
            int targets = 0, requested = 0, failed = 0;
            string firstError = "none";
            Native.EnumWindows(delegate(IntPtr hwnd, IntPtr parameter)
            {
                if (!Native.ClassMatches(hwnd)) return true;
                uint pid;
                if (Native.GetWindowThreadProcessId(hwnd, out pid) == 0) return true;
                IntPtr process = Native.OpenProcess(0x00101000, false, pid);
                if (process == IntPtr.Zero) return true;
                try
                {
                    if (!Native.ProcessMatches(process) || Native.WaitForSingleObject(process, 0) != 258)
                        return true;
                    targets++;
                    UIntPtr reply;
                    Native.SetLastError(0);
                    bool sync = Native.SendMessageTimeout(hwnd, Native.WmUserApplyImeConfig,
                        UIntPtr.Zero, IntPtr.Zero, 0x0022, 1500, out reply) != IntPtr.Zero;
                    int syncError = sync ? 0 : Marshal.GetLastWin32Error();
                    Native.SetLastError(0);
                    bool reload = sync && Native.SendMessageTimeout(hwnd, Native.WmUserApplyImeConfig,
                        new UIntPtr(1), IntPtr.Zero, 0x0022, 1500, out reply) != IntPtr.Zero;
                    int reloadError = reload ? 0 : Marshal.GetLastWin32Error();
                    if (reload) requested++;
                    else
                    {
                        failed++;
                        if (firstError == "none")
                            firstError = "syncError=" + syncError.ToString(CultureInfo.InvariantCulture) +
                                ",reloadError=" + reloadError.ToString(CultureInfo.InvariantCulture);
                    }
                }
                finally { Native.CloseHandle(process); }
                return true;
            }, IntPtr.Zero);
            bool ok = targets > 0 && requested > 0 && failed == 0;
            log.Write("reloadRequested=" + (ok ? "true" : "false") +
                " targets=" + targets.ToString(CultureInfo.InvariantCulture) +
                " requested=" + requested.ToString(CultureInfo.InvariantCulture) +
                " failed=" + failed.ToString(CultureInfo.InvariantCulture) +
                " visualVerified=false error=" + firstError);
        }
    }

    [DataContract]
    internal sealed class OfflineCase
    {
        [DataMember] internal int cardWidthDip;
        [DataMember] internal double dpiScale;
        [DataMember] internal int cardWidthPx;
        [DataMember] internal int bitmapWidth;
        [DataMember] internal int bitmapHeight;
        [DataMember] internal double visibleWidthPx;
        [DataMember] internal int originX;
        [DataMember] internal int originY;
        [DataMember] internal int cardScreenX;
        [DataMember] internal int cardScreenY;
        [DataMember] internal string cacheKey;
        [DataMember] internal bool noPixelsBeyondCutoff;
        [DataMember] internal bool uniformContainAspect;
        [DataMember] internal bool cacheKeyStableWhilePanned;
        [DataMember] internal bool positionRelativeToCardCorrect;
        [DataMember] internal bool liveSizeMismatchRejected;
        [DataMember] internal bool premultipliedAlphaSample;
        [DataMember] internal int rightmostOpaqueScreenX;
        [DataMember] internal double cutoffScreenX;
        [DataMember] internal bool passed;
        [DataMember] internal string preview;
    }

    [DataContract]
    internal sealed class OfflineReport
    {
        [DataMember] internal string mode;
        [DataMember] internal bool realWindowsTouched;
        [DataMember] internal bool actualOverlayStarted;
        [DataMember] internal bool realMachineVerified;
        [DataMember] internal int checks;
        [DataMember] internal int failures;
        [DataMember] internal string fixture;
        [DataMember] internal string note;
        [DataMember] internal List<OfflineCase> cases;
        [DataMember] internal List<string> failureNames;
    }

    internal sealed class SelfTestCounter
    {
        internal int Checks;
        internal int Failures;
        internal readonly List<string> FailureNames = new List<string>();

        internal void Check(bool condition, string name)
        {
            Checks++;
            if (condition) Console.WriteLine("PASS " + name);
            else
            {
                Failures++;
                FailureNames.Add(name);
                Console.WriteLine("FAIL " + name);
            }
        }
    }

    internal static class SelfTests
    {
        internal static int Run(Options options)
        {
            string skinDir = options.SkinDir;
            if (skinDir == null) skinDir = FindFixtureSkin();
            if (skinDir == null || !Directory.Exists(skinDir))
                throw new DirectoryNotFoundException("Legacy fixture skin/kasumi-adaptive was not found");
            skinDir = Path.GetFullPath(skinDir);
            string output = Path.Combine(Environment.CurrentDirectory, "band-overlay-verification", "offline");
            Directory.CreateDirectory(output);

            SelfTestCounter counter = new SelfTestCounter();
            string legacyJson =
                "{\"schemaVersion\":1,\"selectedSkin\":\"kasumi-adaptive\",\"image\":\"assets/band.png\"," +
                "\"widthDip\":1008,\"heightDip\":156,\"offsetXDip\":0,\"offsetYDip\":-79.9,\"cutOffsetXDip\":0}";
            string selfTestConfig = Path.Combine(skinDir, "self-test-band-overlay.json");
            OverlayConfig config = null;
            try { config = OverlayConfig.ParseForDiagnostics(legacyJson, skinDir, selfTestConfig); }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Could not prepare legacy fixture config: " + ex.GetType().Name);
                return 1;
            }
            counter.Check(config.WidthDip == 1008 && config.HeightDip == 156 &&
                config.OffsetXDip == 0 && Math.Abs(config.OffsetYDip + 79.9) < 0.000001 &&
                config.CutOffsetXDip == 0, "band-overlay.json documented values");
            CheckInvalidConfig(counter, skinDir, legacyJson.Replace("\"schemaVersion\":1", "\"schemaVersion\":2"),
                "schemaVersion=2 rejected");
            CheckInvalidConfig(counter, skinDir,
                legacyJson.Replace("\"selectedSkin\":\"kasumi-adaptive\"", "\"selectedSkin\":\"other\""),
                "wrong selectedSkin rejected");
            CheckInvalidConfig(counter, skinDir, legacyJson.Replace("\"assets/band.png\"", "\"../portrait.png\""),
                "escaping image path rejected");
            CheckInvalidConfig(counter, skinDir, legacyJson.Replace("\"cutOffsetXDip\":0", "\"cutOffsetXDip\":-1"),
                "negative cut offset rejected");

            BandAsset asset = BandAsset.Load(config.ImagePath);
            List<OfflineCase> cases = new List<OfflineCase>();
            int[] widths = new int[] { 520, 700, 900 };
            double[] scales = new double[] { 1.0, 1.5, 2.0 };
            try
            {
                foreach (double scale in scales)
                {
                    foreach (int widthDip in widths)
                    {
                        OfflineCase result = RunCase(counter, output, skinDir, config, asset, widthDip, scale);
                        cases.Add(result);
                    }
                }

                OfflineReport report = new OfflineReport
                {
                    mode = "offline-self-test",
                    realWindowsTouched = false,
                    actualOverlayStarted = false,
                    realMachineVerified = false,
                    checks = counter.Checks,
                    failures = counter.Failures,
                    fixture = "skin/kasumi-adaptive/assets/band.png (read-only; skin was not modified)",
                    note = "GDI+ contain/crop and geometry in memory only. This is not a live window, IME, DWM, or real-machine pass.",
                    cases = cases,
                    failureNames = counter.FailureNames
                };
                using (MemoryStream json = new MemoryStream())
                {
                    new DataContractJsonSerializer(typeof(OfflineReport)).WriteObject(json, report);
                    File.WriteAllText(Path.Combine(output, "offline-self-test.json"),
                        Encoding.UTF8.GetString(json.ToArray()), new UTF8Encoding(false));
                }
            }
            finally { asset.Dispose(); }

            Console.WriteLine("OFFLINE_ONLY realWindowsTouched=false actualOverlayStarted=false realMachineVerified=false");
            Console.WriteLine("checks=" + counter.Checks + " failures=" + counter.Failures + " output=" + output);
            return counter.Failures == 0 ? 0 : 1;
        }

        private static void CheckInvalidConfig(SelfTestCounter counter, string skinDir, string json, string name)
        {
            bool rejected = false;
            try { OverlayConfig.ParseForDiagnostics(json, skinDir, Path.Combine(skinDir, "self-test-invalid.json")); }
            catch (Exception) { rejected = true; }
            counter.Check(rejected, name);
        }

        private static OfflineCase RunCase(SelfTestCounter counter, string output, string skinDir,
            OverlayConfig config, BandAsset asset, int widthDip, double scale)
        {
            int dpi = (int)Math.Round(scale * 96.0, MidpointRounding.AwayFromZero);
            int cardWidthPx = (int)Math.Round(widthDip * scale, MidpointRounding.AwayFromZero);
            int cardLeftDip = 16;
            int cardTopDip = 96;
            int hostWidth = cardWidthPx + (int)Math.Round(320 * scale, MidpointRounding.AwayFromZero);
            int hostHeight = (int)Math.Round(320 * scale, MidpointRounding.AwayFromZero);
            int hostX = 100, hostY = 80;
            int cardXRel = (int)Math.Round(cardLeftDip * scale, MidpointRounding.AwayFromZero);
            int cardYRel = (int)Math.Round(cardTopDip * scale, MidpointRounding.AwayFromZero);
            string scaleText = scale.ToString("0.0###", CultureInfo.InvariantCulture);
            string line = "2026-10-20 18:40:49.474 [p4242:t77] candidate-d2d place source=monitor scale=" +
                scaleText + " hwnd_dpi=" + dpi.ToString(CultureInfo.InvariantCulture) +
                " system_dpi=" + dpi.ToString(CultureInfo.InvariantCulture) +
                " remote=0 caret=(10,10) size_px=(" + hostWidth.ToString(CultureInfo.InvariantCulture) + "," +
                hostHeight.ToString(CultureInfo.InvariantCulture) + ") host=(" + hostX.ToString(CultureInfo.InvariantCulture) + "," +
                hostY.ToString(CultureInfo.InvariantCulture) + ") first=(" + hostX.ToString(CultureInfo.InvariantCulture) + "," +
                hostY.ToString(CultureInfo.InvariantCulture) + ") on_screen=true card_px=(" +
                (hostX + cardXRel).ToString(CultureInfo.InvariantCulture) + "," +
                (hostY + cardYRel).ToString(CultureInfo.InvariantCulture) + " " +
                cardWidthPx.ToString(CultureInfo.InvariantCulture) + "w) monitor=(0,0)-(1920,1080)";
            Placement placement = Placement.Parse(line, 1);
            counter.Check(placement != null, "placement parser card=" + widthDip + " scale=" + scaleText);
            if (placement == null) return FailedCase(widthDip, scale);

            Native.RECT liveA = new Native.RECT { Left = hostX, Top = hostY, Right = hostX + hostWidth, Bottom = hostY + hostHeight };
            Native.RECT liveB = new Native.RECT
            {
                Left = hostX + 337, Top = hostY - 121,
                Right = hostX + 337 + hostWidth, Bottom = hostY - 121 + hostHeight
            };
            int cardAX, cardAY, cardBX, cardBY;
            string reasonA, reasonB;
            bool trustA = placement.TryCardFromLiveRect(liveA, out cardAX, out cardAY, out reasonA);
            bool trustB = placement.TryCardFromLiveRect(liveB, out cardBX, out cardBY, out reasonB);
            counter.Check(trustA && trustB && cardBX - cardAX == 337 && cardBY - cardAY == -121,
                "pan follows live host origin with trusted card offset card=" + widthDip + " scale=" + scaleText);
            Native.RECT resized = new Native.RECT { Left = liveA.Left, Top = liveA.Top, Right = liveA.Right + 1, Bottom = liveA.Bottom };
            int ignoredX, ignoredY;
            string resizeReason;
            bool resizeRejected = !placement.TryCardFromLiveRect(resized, out ignoredX, out ignoredY, out resizeReason) &&
                resizeReason == "target-size-mismatch";
            counter.Check(resizeRejected, "stale placement rejects changed host size");
            if (!trustA)
            {
                // Fall back only to record a clearly failed case; the trust test
                // above already reports the failure.
                cardAX = hostX + cardXRel; cardAY = hostY + cardYRel;
            }

            BandLayout layoutA = BandLayout.Compute(config, cardAX, cardAY, placement.CardWidth, placement.Scale);
            BandLayout layoutB = BandLayout.Compute(config, cardBX, cardBY, placement.CardWidth, placement.Scale);
            counter.Check(layoutA != null && layoutB != null, "layout computed card=" + widthDip + " scale=" + scaleText);
            if (layoutA == null || layoutB == null) return FailedCase(widthDip, scale);

            bool positionCorrect = layoutA.OriginX == cardAX + (int)Math.Floor(config.OffsetXDip * scale) &&
                layoutA.OriginY == cardAY + (int)Math.Floor(config.OffsetYDip * scale);
            counter.Check(positionCorrect, "overlay position relative to card card=" + widthDip + " scale=" + scaleText);

            bool cacheStable = String.Equals(layoutA.LocalKey, layoutB.LocalKey, StringComparison.Ordinal);
            counter.Check(cacheStable, "cache key ignores absolute host/card origin card=" + widthDip + " scale=" + scaleText);

            double expectedVisible = Math.Min(config.WidthDip * scale,
                cardWidthPx + (config.CutOffsetXDip - config.OffsetXDip) * scale);
            bool widthCorrect = layoutA.Width == (int)Math.Floor(expectedVisible) &&
                Math.Abs(layoutA.VisibleWidthPx - expectedVisible) < 0.000001;
            counter.Check(widthCorrect, "bitmap width min(fullBand,card+cut-offset) card=" + widthDip + " scale=" + scaleText);

            bool noPixelsBeyondCutoff = layoutA.EndsAtOrBeforeScreenCutoff(config.CutOffsetXDip) &&
                layoutA.Width <= (int)Math.Floor(layoutA.VisibleWidthPx);
            counter.Check(noPixelsBeyondCutoff, "band board ends at or before cutoff card=" + widthDip + " scale=" + scaleText);

            double contain = Math.Min(layoutA.FullWidthPx / asset.Width, layoutA.FullHeightPx / asset.Height);
            double scaledWidth = asset.Width * contain;
            double scaledHeight = asset.Height * contain;
            bool uniformAspect = contain > 0 &&
                Math.Abs(scaledWidth / asset.Width - scaledHeight / asset.Height) < 0.000000001 &&
                scaledWidth <= layoutA.FullWidthPx + 0.000001 &&
                scaledHeight <= layoutA.FullHeightPx + 0.000001;
            counter.Check(uniformAspect, "source is contained without stretch card=" + widthDip + " scale=" + scaleText);

            string stem = "preview-card-" + widthDip.ToString(CultureInfo.InvariantCulture) +
                "-dpi-" + scaleText;
            string relativePreview = "band-overlay-verification/offline/" + stem + ".png";
            string previewPath = Path.Combine(output, stem + ".png");
            bool premultiplied = true;
            int rightmostOpaqueLocal = -1;
            double cutoffScreenX = layoutA.CardScreenX + layoutA.CardWidthPx + config.CutOffsetXDip * scale;
            bool noRenderedPixelBeyondCutoff = false;
            using (RenderedBand rendered = BandRenderer.Render(asset, layoutA))
            {
                counter.Check(Math.Abs(rendered.ContainScale - contain) < 0.000001,
                    "renderer contain scale matches geometry card=" + widthDip + " scale=" + scaleText);
                counter.Check(rendered.Bitmap.Width == layoutA.Width && rendered.Bitmap.Height == layoutA.Height,
                    "rendered bitmap dimensions match board card=" + widthDip + " scale=" + scaleText);
                premultiplied = IsPremultipliedSample(rendered.Bitmap);
                noRenderedPixelBeyondCutoff = ScanCutoffPixels(rendered.Bitmap, layoutA.OriginX,
                    cutoffScreenX, out rightmostOpaqueLocal);
                counter.Check(noRenderedPixelBeyondCutoff,
                    "no rendered alpha pixel crosses cutoff card=" + widthDip + " scale=" + scaleText);
                rendered.Bitmap.Save(previewPath, ImageFormat.Png);
            }
            noPixelsBeyondCutoff = noPixelsBeyondCutoff && noRenderedPixelBeyondCutoff;
            int rightmostOpaqueScreenX = rightmostOpaqueLocal < 0 ? -1 :
                layoutA.OriginX + rightmostOpaqueLocal;

            bool passed = positionCorrect && cacheStable && widthCorrect && noPixelsBeyondCutoff &&
                uniformAspect && trustA && trustB && resizeRejected && premultiplied;
            counter.Check(premultiplied, "rendered pixels are premultiplied BGRA card=" + widthDip + " scale=" + scaleText);
            counter.Check(passed, "offline case passes card=" + widthDip + " scale=" + scaleText);
            OfflineCase data = new OfflineCase
            {
                cardWidthDip = widthDip,
                dpiScale = scale,
                cardWidthPx = cardWidthPx,
                bitmapWidth = layoutA.Width,
                bitmapHeight = layoutA.Height,
                visibleWidthPx = layoutA.VisibleWidthPx,
                originX = layoutA.OriginX,
                originY = layoutA.OriginY,
                cardScreenX = layoutA.CardScreenX,
                cardScreenY = layoutA.CardScreenY,
                cacheKey = layoutA.LocalKey,
                noPixelsBeyondCutoff = noPixelsBeyondCutoff,
                uniformContainAspect = uniformAspect,
                cacheKeyStableWhilePanned = cacheStable,
                positionRelativeToCardCorrect = positionCorrect,
                liveSizeMismatchRejected = resizeRejected,
                premultipliedAlphaSample = premultiplied,
                rightmostOpaqueScreenX = rightmostOpaqueScreenX,
                cutoffScreenX = cutoffScreenX,
                passed = passed,
                preview = relativePreview
            };
            return data;
        }

        private static OfflineCase FailedCase(int widthDip, double scale)
        {
            return new OfflineCase { cardWidthDip = widthDip, dpiScale = scale, passed = false };
        }

        private static bool IsPremultipliedSample(Bitmap bitmap)
        {
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                int stepX = Math.Max(1, bitmap.Width / 37), stepY = Math.Max(1, bitmap.Height / 13);
                byte[] pixel = new byte[4];
                for (int y = 0; y < bitmap.Height; y += stepY)
                    for (int x = 0; x < bitmap.Width; x += stepX)
                    {
                        Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride + x * 4), pixel, 0, 4);
                        int alpha = pixel[3];
                        if (pixel[0] > alpha || pixel[1] > alpha || pixel[2] > alpha) return false;
                    }
                return true;
            }
            finally { bitmap.UnlockBits(data); }
        }

        private static bool ScanCutoffPixels(Bitmap bitmap, int originX, double cutoffScreenX,
            out int rightmostOpaqueLocal)
        {
            rightmostOpaqueLocal = -1;
            bool beyond = false;
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                byte[] row = new byte[data.Stride];
                for (int y = 0; y < bitmap.Height; y++)
                {
                    Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), row, 0, row.Length);
                    for (int x = 0; x < bitmap.Width; x++)
                    {
                        if (row[x * 4 + 3] == 0) continue;
                        if (x > rightmostOpaqueLocal) rightmostOpaqueLocal = x;
                        if ((double)originX + x + 1.0 > cutoffScreenX + 0.000001) beyond = true;
                    }
                }
            }
            finally { bitmap.UnlockBits(data); }
            return !beyond;
        }

        private static string FindFixtureSkin()
        {
            string[] candidates = new string[]
            {
                Path.Combine(Environment.CurrentDirectory, "skin", "kasumi-adaptive"),
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "skin", "kasumi-adaptive"),
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "..", "skin", "kasumi-adaptive")
            };
            foreach (string candidate in candidates)
            {
                try { if (Directory.Exists(candidate)) return Path.GetFullPath(candidate); }
                catch { }
            }
            return null;
        }
    }

    // Targeted frets checks. Kept separate from the original self-test so the
    // old tests are unchanged; invoke this internal harness for frets-only runs.
    internal static class FretSelfTests
    {
        private const string BaseJson =
            "{\"schemaVersion\":1,\"selectedSkin\":\"kasumi-adaptive\",\"image\":\"assets/band.png\"," +
            "\"widthDip\":1008,\"heightDip\":156,\"offsetXDip\":0,\"offsetYDip\":-79.9,\"cutOffsetXDip\":0}";
        private const string FretsJson =
            "{\"schemaVersion\":1,\"selectedSkin\":\"kasumi-adaptive\",\"image\":\"assets/band.png\"," +
            "\"widthDip\":1008,\"heightDip\":156,\"offsetXDip\":0,\"offsetYDip\":-79.9,\"cutOffsetXDip\":0," +
            "\"frets\":{\"cardWidthDip\":664.5,\"toleranceDip\":0.75,\"startXDip\":125," +
            "\"pitchDip\":70.5,\"count\":6,\"topDip\":9,\"bottomDip\":45," +
            "\"lineWidthDip\":0.65,\"lineGapDip\":1.4,\"color\":\"#ffad00\"}}";

        internal static int Run(Options options)
        {
            string skinDir = options != null ? options.SkinDir : null;
            if (skinDir == null) skinDir = FindSkin();
            if (skinDir == null || !Directory.Exists(skinDir))
                throw new DirectoryNotFoundException("Legacy fixture skin/kasumi-adaptive was not found");
            skinDir = Path.GetFullPath(skinDir);
            SelfTestCounter counter = new SelfTestCounter();
            OverlayConfig oldConfig = OverlayConfig.ParseForDiagnostics(BaseJson, skinDir,
                Path.Combine(skinDir, "frets-self-test-base.json"));
            OverlayConfig fretsConfig = OverlayConfig.ParseForDiagnostics(FretsJson, skinDir,
                Path.Combine(skinDir, "frets-self-test.json"));
            counter.Check(oldConfig.Frets == null, "old config without frets remains band-only");
            counter.Check(fretsConfig.Frets != null && fretsConfig.Frets.ColorRgb == 0xFFAD00 &&
                fretsConfig.Frets.Count == 6, "frets JSON parsed");
            CheckInvalid(counter, skinDir, FretsJson.Replace("\"color\":\"#ffad00\"", "\"color\":\"#ffad0\""),
                "frets color rejects non-#RRGGBB");
            CheckInvalid(counter, skinDir, FretsJson.Replace("\"count\":6", "\"count\":21"),
                "frets count >20 rejected");
            CheckInvalid(counter, skinDir, FretsJson.Replace("\"lineWidthDip\":0.65", "\"lineWidthDip\":0"),
                "frets line width zero rejected");

            BandAsset asset = BandAsset.Load(oldConfig.ImagePath);
            try
            {
                foreach (double scale in new double[] { 1.0, 1.5, 2.0 })
                {
                    int cardWidthPx = (int)Math.Round(664.5 * scale, MidpointRounding.AwayFromZero);
                    BandLayout withFrets = BandLayout.Compute(fretsConfig, 240, 400, cardWidthPx, scale);
                    BandLayout baseline = BandLayout.Compute(oldConfig, 240, 400, cardWidthPx, scale);
                    string scaleText = scale.ToString("0.0", CultureInfo.InvariantCulture);
                    counter.Check(withFrets != null && withFrets.FretsActive,
                        "width gate active 664.5 DIP scale=" + scaleText);
                    if (withFrets == null || baseline == null) continue;

                    List<Rectangle> strokeBoxes = ExpectedStrokeBoxes(withFrets);
                    using (RenderedBand frets = BandRenderer.Render(asset, withFrets))
                    using (RenderedBand plain = BandRenderer.Render(asset, baseline))
                    {
                        int changed, outside, hit;
                        ComparePixels(frets.Bitmap, plain.Bitmap, strokeBoxes, out changed, out outside, out hit);
                        int twoSided = CountTwoSidedBoundaries(frets.Bitmap, plain.Bitmap, withFrets);
                        bool twoLines = strokeBoxes.Count == withFrets.Frets.Count * 2 &&
                            changed > 0 && hit == strokeBoxes.Count &&
                            twoSided == withFrets.Frets.Count;
                        counter.Check(twoLines, "two fret-line pixels per boundary scale=" + scaleText);
                        counter.Check(outside == 0, "non-frets pixels unchanged scale=" + scaleText);

                        string cacheKey = frets.CacheKey;
                        BandLayout panned = BandLayout.Compute(fretsConfig, 240 + 337, 400 - 121,
                            cardWidthPx, scale);
                        if (panned == null)
                        {
                            counter.Check(false, "panning layout scale=" + scaleText);
                        }
                        else
                        {
                            using (RenderedBand moved = BandRenderer.Render(asset, panned))
                                counter.Check(String.Equals(cacheKey, moved.CacheKey, StringComparison.Ordinal),
                                    "panning keeps frets cache key scale=" + scaleText);
                        }
                    }
                }

                // Long text can expand the total card while item boundaries are
                // absent from diagnostics. Frets must disappear, not be guessed.
                int mismatchPx = (int)Math.Round((664.5 + 0.75 + 2.0) * 2.0,
                    MidpointRounding.AwayFromZero);
                BandLayout mismatch = BandLayout.Compute(fretsConfig, 240, 400, mismatchPx, 2.0);
                BandLayout mismatchBase = BandLayout.Compute(oldConfig, 240, 400, mismatchPx, 2.0);
                counter.Check(mismatch != null && !mismatch.FretsActive,
                    "expanded card hides frets");
                if (mismatch != null && mismatchBase != null)
                {
                    using (RenderedBand frets = BandRenderer.Render(asset, mismatch))
                    using (RenderedBand plain = BandRenderer.Render(asset, mismatchBase))
                        counter.Check(CountDifferentPixels(frets.Bitmap, plain.Bitmap) == 0,
                            "hidden frets leave band pixels unchanged");
                }
            }
            finally { asset.Dispose(); }

            Console.WriteLine("FRETS_OFFLINE checks=" + counter.Checks +
                " failures=" + counter.Failures + " oldSelfTestRerun=false");
            return counter.Failures == 0 ? 0 : 1;
        }

        private static void CheckInvalid(SelfTestCounter counter, string skinDir, string json, string name)
        {
            bool rejected = false;
            try { OverlayConfig.ParseForDiagnostics(json, skinDir, Path.Combine(skinDir, "frets-invalid.json")); }
            catch (Exception) { rejected = true; }
            counter.Check(rejected, name);
        }

        private static List<Rectangle> ExpectedStrokeBoxes(BandLayout layout)
        {
            List<Rectangle> boxes = new List<Rectangle>();
            FretSpec frets = layout.Frets;
            if (frets == null || !layout.FretsActive) return boxes;
            double top = layout.CardLocalY + frets.TopDip * layout.Scale;
            double bottom = layout.CardLocalY + frets.BottomDip * layout.Scale;
            double lineWidthPx = frets.LineWidthDip * layout.Scale;
            double gapPx = frets.LineGapDip * layout.Scale;
            for (int i = 0; i < frets.Count; i++)
            {
                double boundary = layout.CardLocalX + (frets.StartXDip + frets.PitchDip * (i + 1)) * layout.Scale;
                for (int side = -1; side <= 1; side += 2)
                {
                    double center = boundary + side * gapPx * 0.5;
                    int left = Math.Max(0, (int)Math.Floor(center - lineWidthPx * 0.5));
                    int right = Math.Min(layout.Width, (int)Math.Ceiling(center + lineWidthPx * 0.5));
                    int topPx = Math.Max(0, (int)Math.Floor(top));
                    int bottomPx = Math.Min(layout.Height, (int)Math.Ceiling(bottom));
                    if (right > left && bottomPx > topPx)
                        boxes.Add(new Rectangle(left, topPx, right - left, bottomPx - topPx));
                }
            }
            return boxes;
        }

        private static void ComparePixels(Bitmap actual, Bitmap baseline, List<Rectangle> strokeBoxes,
            out int changed, out int outside, out int strokesHit)
        {
            changed = outside = strokesHit = 0;
            bool[] hits = new bool[strokeBoxes.Count];
            BitmapData a = actual.LockBits(new Rectangle(0, 0, actual.Width, actual.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                BitmapData b = baseline.LockBits(new Rectangle(0, 0, baseline.Width, baseline.Height),
                    ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    byte[] rowA = new byte[a.Stride];
                    byte[] rowB = new byte[b.Stride];
                    for (int y = 0; y < actual.Height; y++)
                    {
                        Marshal.Copy(IntPtr.Add(a.Scan0, y * a.Stride), rowA, 0, rowA.Length);
                        Marshal.Copy(IntPtr.Add(b.Scan0, y * b.Stride), rowB, 0, rowB.Length);
                        for (int x = 0; x < actual.Width; x++)
                        {
                            int offset = x * 4;
                            if (rowA[offset] == rowB[offset] && rowA[offset + 1] == rowB[offset + 1] &&
                                rowA[offset + 2] == rowB[offset + 2] && rowA[offset + 3] == rowB[offset + 3])
                                continue;
                            changed++;
                            bool inLine = false;
                            for (int box = 0; box < strokeBoxes.Count; box++)
                            {
                                if (!strokeBoxes[box].Contains(x, y)) continue;
                                inLine = true;
                                hits[box] = true;
                            }
                            if (!inLine) outside++;
                        }
                    }
                }
                finally { baseline.UnlockBits(b); }
            }
            finally { actual.UnlockBits(a); }
            for (int i = 0; i < hits.Length; i++) if (hits[i]) strokesHit++;
        }

        private static int CountDifferentPixels(Bitmap actual, Bitmap baseline)
        {
            int changed = 0;
            BitmapData a = actual.LockBits(new Rectangle(0, 0, actual.Width, actual.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                BitmapData b = baseline.LockBits(new Rectangle(0, 0, baseline.Width, baseline.Height),
                    ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    byte[] rowA = new byte[a.Stride];
                    byte[] rowB = new byte[b.Stride];
                    for (int y = 0; y < actual.Height; y++)
                    {
                        Marshal.Copy(IntPtr.Add(a.Scan0, y * a.Stride), rowA, 0, rowA.Length);
                        Marshal.Copy(IntPtr.Add(b.Scan0, y * b.Stride), rowB, 0, rowB.Length);
                        for (int x = 0; x < actual.Width * 4; x++)
                            if (rowA[x] != rowB[x]) { changed++; break; }
                    }
                }
                finally { baseline.UnlockBits(b); }
            }
            finally { actual.UnlockBits(a); }
            return changed;
        }

        private static int CountTwoSidedBoundaries(Bitmap actual, Bitmap baseline, BandLayout layout)
        {
            FretSpec frets = layout.Frets;
            if (frets == null || !layout.FretsActive || frets.Count <= 0) return 0;
            bool[] left = new bool[frets.Count];
            bool[] right = new bool[frets.Count];
            double gapPx = frets.LineGapDip * layout.Scale;
            double widthPx = frets.LineWidthDip * layout.Scale;
            double reach = gapPx * 0.5 + widthPx * 0.5 + 1.5;
            BitmapData a = actual.LockBits(new Rectangle(0, 0, actual.Width, actual.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
            try
            {
                BitmapData b = baseline.LockBits(new Rectangle(0, 0, baseline.Width, baseline.Height),
                    ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
                try
                {
                    byte[] rowA = new byte[a.Stride];
                    byte[] rowB = new byte[b.Stride];
                    for (int y = 0; y < actual.Height; y++)
                    {
                        Marshal.Copy(IntPtr.Add(a.Scan0, y * a.Stride), rowA, 0, rowA.Length);
                        Marshal.Copy(IntPtr.Add(b.Scan0, y * b.Stride), rowB, 0, rowB.Length);
                        for (int x = 0; x < actual.Width; x++)
                        {
                            int offset = x * 4;
                            if (rowA[offset] == rowB[offset] && rowA[offset + 1] == rowB[offset + 1] &&
                                rowA[offset + 2] == rowB[offset + 2] && rowA[offset + 3] == rowB[offset + 3])
                                continue;
                            double pixelCenter = x + 0.5;
                            for (int i = 0; i < frets.Count; i++)
                            {
                                double boundary = layout.CardLocalX +
                                    (frets.StartXDip + frets.PitchDip * (i + 1)) * layout.Scale;
                                if (pixelCenter < boundary - reach || pixelCenter > boundary + reach) continue;
                                if (pixelCenter < boundary) left[i] = true;
                                else if (pixelCenter > boundary) right[i] = true;
                            }
                        }
                    }
                }
                finally { baseline.UnlockBits(b); }
            }
            finally { actual.UnlockBits(a); }
            int both = 0;
            for (int i = 0; i < frets.Count; i++) if (left[i] && right[i]) both++;
            return both;
        }

        private static string FindSkin()
        {
            string[] candidates = new string[]
            {
                Path.Combine(Environment.CurrentDirectory, "skin", "kasumi-adaptive"),
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "skin", "kasumi-adaptive")
            };
            foreach (string candidate in candidates)
            {
                try { if (Directory.Exists(candidate)) return Path.GetFullPath(candidate); }
                catch { }
            }
            return null;
        }
    }

    // Targeted atlas checks, separate from the original 131 and the frets set.
    internal static class AtlasSelfTests
    {
        internal static int Run()
        {
            SelfTestCounter counter = new SelfTestCounter();
            int[][] runs = new int[][] { new int[] { 100, 720, 740 } };
            AtlasSpec spec = new AtlasSpec(2172, 724, 350, 605, 710, 1560, 1735,
                runs, "test-exclusions.json", null, "test");
            OverlayConfig atlasConfig = new OverlayConfig
            {
                WidthDip = 2172, HeightDip = 724, OffsetXDip = 0, OffsetYDip = -293,
                CutOffsetXDip = 0, Atlas = spec
            };
            BandLayout max = BandLayout.Compute(atlasConfig, 100, 200, 1735, 1.0);
            counter.Check(max != null && max.AtlasMode && max.Atlas.Segments.Length == 5,
                "atlas has five segments at max design");
            if (max == null) return Finish(counter);

            double[] sourceX = new double[] { 0, 350, 605, 710, 1560 };
            double[] sourceWidth = new double[] { 350, 255, 105, 850, 612 };
            bool continuous = true;
            for (int i = 0; i < max.Atlas.Segments.Length; i++)
            {
                AtlasSegment segment = max.Atlas.Segments[i];
                continuous &= Math.Abs(segment.DestinationX - sourceX[i]) < 0.000001;
                continuous &= Math.Abs(segment.DestinationWidth - sourceWidth[i]) < 0.000001;
                continuous &= Math.Abs(segment.ScaleX - 1.0) < 0.000001;
                continuous &= Math.Abs(segment.ScaleY - 1.0) < 0.000001;
                continuous &= Math.Abs(segment.DestinationHeight - 724) < 0.000001;
                if (i > 0)
                {
                    AtlasSegment previous = max.Atlas.Segments[i - 1];
                    continuous &= Math.Abs(segment.DestinationX -
                        (previous.DestinationX + previous.DestinationWidth)) < 0.000001;
                }
            }
            counter.Check(continuous, "max atlas slices continuous and x/y isotropic");

            int[] cards = new int[] { 1200, 1500, 1735 };
            bool fixedSlicesStable = true;
            foreach (int card in cards)
            {
                BandLayout layout = BandLayout.Compute(atlasConfig, 100, 200, card, 1.0);
                if (layout == null) { fixedSlicesStable = false; continue; }
                int[] fixedIndices = new int[] { 0, 2, 4 };
                for (int i = 0; i < fixedIndices.Length; i++)
                {
                    AtlasSegment segment = layout.Atlas.Segments[fixedIndices[i]];
                    fixedSlicesStable &= Math.Abs(segment.DestinationWidth - sourceWidth[fixedIndices[i]]) < 0.000001;
                    fixedSlicesStable &= Math.Abs(segment.DestinationHeight - 724) < 0.000001;
                }
            }
            counter.Check(fixedSlicesStable, "three fixed slices keep width/height");

            foreach (double dpi in new double[] { 1.0, 1.5, 2.0 })
            {
                BandLayout a = BandLayout.Compute(atlasConfig, 100, 200, 1735, dpi);
                BandLayout b = BandLayout.Compute(atlasConfig, 433, 89, 1735, dpi);
                bool stable = a != null && b != null &&
                    String.Equals(a.LocalKey, b.LocalKey, StringComparison.Ordinal);
                counter.Check(stable, "atlas panning keeps key DPI=" +
                    dpi.ToString("0.0", CultureInfo.InvariantCulture));
            }

            using (Region excluded = BandRenderer.BuildExclusionRegion(max.Atlas.Segments[3], max.Atlas))
            using (Bitmap test = new Bitmap(max.Width, max.Height, PixelFormat.Format32bppPArgb))
            {
                using (Graphics graphics = Graphics.FromImage(test))
                {
                    graphics.Clear(Color.Black);
                    if (excluded != null)
                    {
                        graphics.ExcludeClip(excluded);
                        graphics.FillRectangle(Brushes.White, 0, 0, test.Width, test.Height);
                        graphics.ResetClip();
                    }
                }
                bool insideKept = test.GetPixel(725, 100).ToArgb() == Color.Black.ToArgb();
                bool leftKept = test.GetPixel(719, 100).ToArgb() == Color.White.ToArgb();
                bool rightKept = test.GetPixel(741, 100).ToArgb() == Color.White.ToArgb();
                bool rowOutsideKept = test.GetPixel(725, 101).ToArgb() == Color.White.ToArgb();
                counter.Check(insideKept && leftKept && rightKept && rowOutsideKept,
                    "exclusion runs only clear their mapped pixels");
            }

            OverlayConfig legacy = new OverlayConfig
            {
                WidthDip = 1008, HeightDip = 156, OffsetXDip = 0, OffsetYDip = -79.9,
                CutOffsetXDip = 0
            };
            BandLayout legacyLayout = BandLayout.Compute(legacy, 120, 80, 520, 1.0);
            counter.Check(legacyLayout != null && !legacyLayout.AtlasMode &&
                legacyLayout.Width == 520, "legacy mode remains without atlas");

            bool assetRejected = false;
            try { spec.ValidateAsset(2171, 724); }
            catch (InvalidDataException) { assetRejected = true; }
            counter.Check(assetRejected, "atlas asset dimensions must match source");

            return Finish(counter);
        }

        private static int Finish(SelfTestCounter counter)
        {
            Console.WriteLine("ATLAS_OFFLINE checks=" + counter.Checks +
                " failures=" + counter.Failures +
                " oldSelfTestRerun=false fretsSelfTestRerun=false");
            return counter.Failures == 0 ? 0 : 1;
        }
    }

    internal static class Probe
    {
        internal static int Run(Options options)
        {
            string skinDir = options.SkinDir;
            if (skinDir == null) skinDir = FindSkin();
            string dataDir = options.DataDir != null ? options.DataDir :
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "metasequoiaime");
            Console.WriteLine("diagnosticMode=probe resident=false guiStarted=false");
            bool selected = false;
            try { selected = SkinSelection.IsSelected(dataDir); }
            catch (Exception ex) { Console.WriteLine("configSelected=false error=" + ex.GetType().Name); }
            Console.WriteLine("configSelected=" + Bool(selected));
            OverlayConfig config = null;
            try
            {
                config = OverlayConfig.Load(skinDir);
                using (BandAsset asset = BandAsset.Load(config.ImagePath))
                {
                    config.ValidateAsset(asset);
                    Console.WriteLine("bandConfig=loaded schemaVersion=1 selectedSkin=kasumi-adaptive image=" +
                        asset.Width + "x" + asset.Height + " widthDip=" +
                        config.WidthDip.ToString("R", CultureInfo.InvariantCulture) + " heightDip=" +
                        config.HeightDip.ToString("R", CultureInfo.InvariantCulture) +
                        " offsetX=" + config.OffsetXDip.ToString("R", CultureInfo.InvariantCulture) +
                        " offsetY=" + config.OffsetYDip.ToString("R", CultureInfo.InvariantCulture) +
                        " cutOffset=" + config.CutOffsetXDip.ToString("R", CultureInfo.InvariantCulture) +
                        " atlasMode=" + (config.Atlas == null ? "false" : "true") +
                        (config.Atlas == null ? "" : " sourceWidthPx=" +
                            config.Atlas.SourceWidthPx.ToString(CultureInfo.InvariantCulture) +
                            " sourceHeightPx=" + config.Atlas.SourceHeightPx.ToString(CultureInfo.InvariantCulture)));
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine("bandConfig=missing-or-invalid error=" + ex.GetType().Name);
            }

            Placement placement = null;
            try { placement = ReadLatestPlacement(options.DiagnosticLog); }
            catch (Exception ex) { Console.WriteLine("diagnosticRead=false error=" + ex.GetType().Name); }
            if (placement == null) Console.WriteLine("latestNumericPlacement=none");
            else
            {
                Console.WriteLine("latestNumericPlacement pid=" + placement.Pid +
                    " scale=" + placement.Scale.ToString("R", CultureInfo.InvariantCulture) +
                    " hwndDpi=" + placement.HwndDpi + " size=" + placement.Width + "x" + placement.Height +
                    " host=(" + placement.HostX + "," + placement.HostY + ") card=(" +
                    placement.CardX + "," + placement.CardY + " " + placement.CardWidth + "w)");
            }

            IntPtr previousDpiContext = Native.GetThreadDpiAwarenessContext();
            IntPtr changedDpiContext = Native.SetThreadDpiAwarenessContext(new IntPtr(-4));
            int probeAwareness = Native.GetAwarenessFromDpiAwarenessContext(Native.GetThreadDpiAwarenessContext());
            Console.WriteLine("probeThreadDpiAwareness=" + probeAwareness +
                " dpiContextChanged=" + Bool(changedDpiContext != IntPtr.Zero));
            int candidates = 0;
            try
            {
                Native.EnumWindows(delegate(IntPtr hwnd, IntPtr parameter)
                {
                    if (!Native.ClassMatches(hwnd)) return true;
                    candidates++;
                    uint pid;
                    Native.GetWindowThreadProcessId(hwnd, out pid);
                    long exStyle = Native.GetWindowLongPtr(hwnd, Native.GwlExStyle).ToInt64();
                    Native.RECT rect;
                    bool rectOk = Native.GetWindowRect(hwnd, out rect);
                    int cloaked = 0;
                    int dwm = Native.DwmGetWindowAttribute(hwnd, Native.DwmwaCloaked, out cloaked, 4);
                    IntPtr process = Native.OpenProcess(0x00101000, false, pid);
                    bool official = false, alive = false;
                    try
                    {
                        if (process != IntPtr.Zero)
                        {
                            official = Native.ProcessMatches(process);
                            alive = Native.WaitForSingleObject(process, 0) == 258;
                        }
                    }
                    finally { if (process != IntPtr.Zero) Native.CloseHandle(process); }
                    Console.WriteLine("candidate hwnd=" + hwnd.ToInt64() + " pid=" + pid +
                        " visible=" + Bool(Native.IsWindowVisible(hwnd)) +
                        " noRedirection=" + Bool((exStyle & Native.WsExNoRedirectionBitmap) != 0) +
                        " topmost=" + Bool((exStyle & Native.WsExTopmost) != 0) +
                        " officialProcess=" + Bool(official) + " alive=" + Bool(alive) +
                        " dpi=" + Native.GetDpiForWindow(hwnd) +
                        " rectOk=" + Bool(rectOk) +
                        " rect=" + (rectOk ? "(" + rect.Left + "," + rect.Top + " " +
                            (rect.Right - rect.Left) + "x" + (rect.Bottom - rect.Top) + ")" : "none") +
                        " dwmQueryOk=" + Bool(dwm == 0) + " cloaked=" + cloaked +
                        " topLevelRectMatch=" + Bool(placement != null && rectOk &&
                            rect.Right - rect.Left == placement.Width && rect.Bottom - rect.Top == placement.Height));
                    return true;
                }, IntPtr.Zero);
            }
            finally
            {
                if (changedDpiContext != IntPtr.Zero)
                    Native.SetThreadDpiAwarenessContext(previousDpiContext);
            }
            Console.WriteLine("officialClassCaptionCount=" + candidates);
            Console.WriteLine("targetWindowWritesNotAttempted=true setWindowRgnPresent=false residentStartBlocked=true");
            return 0;
        }

        private static string FindSkin()
        {
            string[] candidates = new string[]
            {
                Path.Combine(Environment.CurrentDirectory, "skin", "kasumi-adaptive"),
                Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", "skin", "kasumi-adaptive")
            };
            foreach (string candidate in candidates)
            {
                try { if (Directory.Exists(candidate)) return Path.GetFullPath(candidate); }
                catch { }
            }
            return Path.Combine(Environment.CurrentDirectory, "skin", "kasumi-adaptive");
        }

        private static Placement ReadLatestPlacement(string path)
        {
            using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                FileShare.ReadWrite | FileShare.Delete))
            {
                byte[] bom = new byte[3];
                int bomLength = stream.Read(bom, 0, bom.Length);
                bool utf16 = bomLength >= 2 &&
                    ((bom[0] == 255 && bom[1] == 254) || (bom[0] == 254 && bom[1] == 255));
                Encoding encoding = utf16 ? (bom[0] == 255 ? Encoding.Unicode : Encoding.BigEndianUnicode) :
                    new UTF8Encoding(false, false);
                long start = Math.Max(0, stream.Length - 65536);
                if (utf16 && (start % 2) != 0) start++;
                stream.Position = start;
                using (StreamReader reader = new StreamReader(stream, encoding, start == 0))
                {
                    if (start != 0) reader.ReadLine();
                    Placement latest = null;
                    string line;
                    while ((line = reader.ReadLine()) != null)
                    {
                        if (line.Length > 4096) continue;
                        Placement parsed = Placement.Parse(line, 0);
                        if (parsed != null) latest = parsed;
                    }
                    return latest;
                }
            }
        }

        private static string Bool(bool value) { return value ? "true" : "false"; }
    }

    internal static class Program
    {
        private const string InstanceMutexName = @"Local\KasumiBandOverlay-v1";

        [STAThread]
        private static int Main(string[] args)
        {
            bool selfTestRequested = Array.IndexOf(args, "--self-test") >= 0;
            try
            {
                Options options = Options.Parse(args);
#if DIAGNOSTIC_ONLY
                if (options.SelfTest == options.Probe)
                    throw new ArgumentException("Diagnostics allows exactly one of --self-test or --probe");
                if (options.ReloadSkin)
                    throw new ArgumentException("Diagnostics cannot request a skin reload");
                if (options.ReplaceRunning)
                    throw new ArgumentException("Diagnostics cannot use --replace-running");
                if (options.SelfTest) return SelfTests.Run(options);
                return Probe.Run(options);
#else
                if (options.SelfTest || options.Probe)
                    throw new ArgumentException("Use KasumiBandOverlay.Diagnostics.exe for --self-test/--probe");
                return RunResident(options);
#endif
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("KasumiBandOverlay: " + ex.GetType().Name);
                if (selfTestRequested) Console.Error.WriteLine(ex.ToString());
                return 1;
            }
        }

#if !DIAGNOSTIC_ONLY
        private static int RunResident(Options options)
        {
            if (!Directory.Exists(options.SkinDir) || !Directory.Exists(options.DataDir))
                throw new DirectoryNotFoundException("Skin or data directory was not found");
            LogWriter log = new LogWriter(options.DataDir);
            // Reject invalid replacement inputs before stopping the resident.
            // This uses the same parser and decoder as the eventual startup.
            try
            {
                OverlayConfig preflight = OverlayConfig.Load(options.SkinDir);
                using (BandAsset verifiedAsset = BandAsset.Load(preflight.ImagePath))
                {
                    preflight.ValidateAsset(verifiedAsset);
                }
                log.Write("startupPreflight=true");
            }
            catch (Exception ex)
            {
                log.Write("error=startup-preflight-failed detail=" + ex.GetType().Name);
                return 3;
            }
            // Explicit upgrade replacement: signal the existing overlay family
            // and wait for normal exit before acquiring the single-instance mutex.
            // The old process is never killed or reconfigured.
            if (options.ReplaceRunning)
            {
                try { ReplaceRunningGuard.SignalAndWait(log); }
                catch (Exception ex)
                {
                    log.Write("error=replace-running-failed detail=" + ex.GetType().Name);
                    return 2;
                }
            }
            bool mutexCreated;
            using (Mutex mutex = new Mutex(true, InstanceMutexName, out mutexCreated))
            {
                if (!mutexCreated)
                {
                    log.Write("error=already-running");
                    return 2;
                }
                bool releaseMutex = true;
                try
                {
                    log.Write("startup skinDir=" + options.SkinDir + " dataDir=" + options.DataDir +
                        " diagnosticLog=" + options.DiagnosticLog +
                        " reloadSkin=" + (options.ReloadSkin ? "true" : "false"));
                    EnsurePerMonitorV2(log);

                    // Replace the old region-based helper before creating any
                    // overlay state. The old helper receives only its named stop
                    // event; it is never terminated by force.
                    OldClipperGuard.StopAndWait(log);

                    OverlayConfig config;
                    BandAsset asset;
                    try
                    {
                        config = OverlayConfig.Load(options.SkinDir);
                        asset = BandAsset.Load(config.ImagePath);
                        config.ValidateAsset(asset);
                        log.Write("configLoaded=true image=" + asset.Width + "x" + asset.Height +
                            " widthDip=" + config.WidthDip.ToString("R", CultureInfo.InvariantCulture) +
                            " heightDip=" + config.HeightDip.ToString("R", CultureInfo.InvariantCulture) +
                            " cutOffsetXDip=" + config.CutOffsetXDip.ToString("R", CultureInfo.InvariantCulture) +
                            " atlasMode=" + (config.Atlas == null ? "false" : "true") +
                            (config.Atlas == null ? "" : " sourceWidthPx=" +
                                config.Atlas.SourceWidthPx.ToString(CultureInfo.InvariantCulture) +
                                " sourceHeightPx=" + config.Atlas.SourceHeightPx.ToString(CultureInfo.InvariantCulture)));
                    }
                    catch (Exception ex)
                    {
                        log.Write("error=config-load-failed detail=" + ex.GetType().Name);
                        return 3;
                    }

                    bool assetOwnedByContext = false;
                    try
                    {
                        using (OverlayContext context = new OverlayContext(options, config, asset, log))
                        {
                            assetOwnedByContext = true;
                            context.Run();
                        }
                    }
                    finally
                    {
                        if (!assetOwnedByContext) asset.Dispose();
                    }
                    return 0;
                }
                catch (Exception ex)
                {
                    log.Write("error=startup-or-resident-failed detail=" + ex.GetType().Name);
                    return 2;
                }
                finally
                {
                    try { if (releaseMutex) mutex.ReleaseMutex(); }
                    catch { }
                }
            }
        }

        private static void EnsurePerMonitorV2(LogWriter log)
        {
            Native.SetLastError(0);
            bool call = Native.SetProcessDpiAwarenessContext(new IntPtr(-4));
            int callError = call ? 0 : Marshal.GetLastWin32Error();
            int awareness = Native.GetAwarenessFromDpiAwarenessContext(Native.GetThreadDpiAwarenessContext());
            log.Write("dpiAwareness=" + awareness + " setCall=" + (call ? "true" : "false") +
                " setError=" + callError);
            if (awareness != 2)
                throw new InvalidOperationException("Per-monitor V2 DPI awareness required");
        }
#endif
    }
}
