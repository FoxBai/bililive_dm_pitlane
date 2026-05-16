using System.IO;
using System.Text.Json;
using PitlaneDanmaku.Windows.Models;

namespace PitlaneDanmaku.Windows.Services;

public sealed class AssetCatalog
{
    private readonly Random _random = new();

    public AssetCatalog(LogService log)
    {
        RootPath = Path.Combine(AppContext.BaseDirectory, "Assets");
        Cars = LoadCars(log);
        CommentFramePath = Path.Combine(RootPath, "comment-box", "comment_frame.png");
    }

    public string RootPath { get; }

    public IReadOnlyList<CarAsset> Cars { get; }

    public string CommentFramePath { get; }

    public CarAsset PickCar()
    {
        if (Cars.Count == 0)
        {
            throw new InvalidOperationException("没有找到赛车素材。");
        }

        lock (_random)
        {
            return Cars[_random.Next(Cars.Count)];
        }
    }

    public string ToWebPath(CarAsset car)
    {
        var relative = Path.GetRelativePath(RootPath, car.AbsolutePath).Replace('\\', '/');
        return "/assets/" + relative;
    }

    public string ResolveAssetPath(string requestPath)
    {
        var relative = requestPath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
        if (relative.StartsWith("assets" + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
        {
            relative = relative["assets/".Length..];
        }

        var fullPath = Path.GetFullPath(Path.Combine(RootPath, relative));
        var root = Path.GetFullPath(RootPath);
        if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("非法素材路径。");
        }

        return fullPath;
    }

    private IReadOnlyList<CarAsset> LoadCars(LogService log)
    {
        var carsDirectory = Path.Combine(RootPath, "cars");
        var manifestPath = Path.Combine(carsDirectory, "cars.json");
        var cars = new List<CarAsset>();

        if (File.Exists(manifestPath))
        {
            using var document = JsonDocument.Parse(File.ReadAllText(manifestPath));
            foreach (var item in document.RootElement.EnumerateArray())
            {
                var id = item.GetProperty("id").GetString() ?? $"car_{cars.Count + 1:00}";
                var file = item.GetProperty("file").GetString() ?? "";
                var width = item.TryGetProperty("width", out var widthElement) ? widthElement.GetInt32() : 555;
                var height = item.TryGetProperty("height", out var heightElement) ? heightElement.GetInt32() : 215;
                var relative = file.TrimStart('/', '\\').Replace('/', Path.DirectorySeparatorChar);
                var absolute = Path.Combine(RootPath, relative);
                if (File.Exists(absolute))
                {
                    cars.Add(new CarAsset(id, Path.GetFileName(absolute), absolute, width, height));
                }
            }
        }

        if (cars.Count == 0 && Directory.Exists(carsDirectory))
        {
            foreach (var file in Directory.EnumerateFiles(carsDirectory, "*.png").OrderBy(static path => path))
            {
                cars.Add(new CarAsset(Path.GetFileNameWithoutExtension(file), Path.GetFileName(file), file, 555, 215));
            }
        }

        log.Info($"已加载 {cars.Count} 个赛车素材。");
        return cars;
    }
}
