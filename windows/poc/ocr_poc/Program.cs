using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using Windows.Globalization;
using Windows.Graphics.Imaging;
using Windows.Media.Ocr;
using Windows.Storage.Streams;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;
        string? imagePath = null;
        for (var i = 0; i < args.Length; ++i)
        {
            if (args[i] == "--image" && i + 1 < args.Length)
            {
                imagePath = args[++i];
            }
            else if (args[i] == "--help" || args[i] == "-h")
            {
                Console.WriteLine("usage: ocr_poc [--image PATH]");
                return 0;
            }
        }

        try
        {
            foreach (var language in OcrEngine.AvailableRecognizerLanguages)
            {
                Console.WriteLine($"[ocr_poc] language: {language.LanguageTag} ({language.DisplayName})");
            }
        }
        catch (Exception error)
        {
            Console.WriteLine($"[ocr_poc] no OCR language packs: {error.Message}");
            return 1;
        }

        var engine = OcrEngine.TryCreateFromLanguage(new Language("zh-Hans"))
                     ?? OcrEngine.TryCreateFromUserProfileLanguages();
        if (engine is null)
        {
            Console.Error.WriteLine("[ocr_poc] no OCR engine available");
            return 1;
        }
        Console.WriteLine($"[ocr_poc] engine: {engine.RecognizerLanguage.LanguageTag}");

        if (imagePath is null)
        {
            imagePath = Path.Combine(Path.GetTempPath(), "livezoom_ocr_poc.png");
            CaptureScreen(imagePath);
            Console.WriteLine($"[ocr_poc] captured {imagePath}");
        }

        using var stream = File.OpenRead(imagePath);
        using var randomAccess = stream.AsRandomAccessStream();
        var decoder = await BitmapDecoder.CreateAsync(randomAccess);
        using var bitmap = await decoder.GetSoftwareBitmapAsync(
            BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);

        var started = DateTime.UtcNow;
        var result = await engine.RecognizeAsync(bitmap);
        var elapsed = (DateTime.UtcNow - started).TotalMilliseconds;
        Console.WriteLine($"[ocr_poc] {result.Lines.Count} line(s) in {elapsed:F0} ms");

        foreach (var line in result.Lines)
        {
            var rects = line.Words.Select(word => word.BoundingRect).ToList();
            if (rects.Count == 0) continue;
            var left = rects.Min(rect => rect.X);
            var top = rects.Min(rect => rect.Y);
            var right = rects.Max(rect => rect.X + rect.Width);
            var bottom = rects.Max(rect => rect.Y + rect.Height);
            Console.WriteLine($"{left:F0},{top:F0},{right - left:F0},{bottom - top:F0}\t{line.Text}");
        }
        return 0;
    }

    private static void CaptureScreen(string path)
    {
        var bounds = Screen.PrimaryScreen!.Bounds;
        using var bitmap = new Bitmap(bounds.Width, bounds.Height);
        using var graphics = Graphics.FromImage(bitmap);
        graphics.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size);
        bitmap.Save(path, ImageFormat.Png);
    }
}
