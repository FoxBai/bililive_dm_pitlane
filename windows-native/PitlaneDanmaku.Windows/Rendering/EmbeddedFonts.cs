using System.IO;
using System.Windows.Media;

namespace PitlaneDanmaku.Windows.Rendering;

public static class EmbeddedFonts
{
    public const string HarmonySansScFamilyName = "HarmonyOS Sans SC";
    public const string HarmonySansScRegularWebPath = "/assets/fonts/HarmonyOS_Sans_SC_Regular.ttf";
    public const string HarmonySansScBoldWebPath = "/assets/fonts/HarmonyOS_Sans_SC_Bold.ttf";

    public static FontFamily HarmonySansSc { get; } = CreateHarmonySansSc();

    public static string HarmonySansScCss =>
        $$"""
        @font-face {
          font-family: "{{HarmonySansScFamilyName}}";
          src: url("{{HarmonySansScRegularWebPath}}") format("truetype");
          font-weight: 400;
          font-style: normal;
          font-display: block;
        }
        @font-face {
          font-family: "{{HarmonySansScFamilyName}}";
          src: url("{{HarmonySansScBoldWebPath}}") format("truetype");
          font-weight: 700;
          font-style: normal;
          font-display: block;
        }
        """;

    private static FontFamily CreateHarmonySansSc()
    {
        var fontDirectory = Path.Combine(AppContext.BaseDirectory, "Assets", "fonts") + Path.DirectorySeparatorChar;
        return new FontFamily(new Uri(fontDirectory, UriKind.Absolute), "./#HarmonyOS Sans SC");
    }
}
