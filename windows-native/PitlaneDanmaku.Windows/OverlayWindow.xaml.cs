using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using PitlaneDanmaku.Windows.Models;
using PitlaneDanmaku.Windows.Services;

namespace PitlaneDanmaku.Windows;

public partial class OverlayWindow : Window
{
    private const double BaseWidth = 1040;
    private const double BaseHeight = 500;
    private const double BaseGap = 24;
    private readonly AssetCatalog _assets;
    private readonly DispatcherTimer _timer;
    private readonly List<RaceVisual> _items = [];
    private AppSettings _settings;

    public OverlayWindow(AssetCatalog assets, AppSettings settings)
    {
        InitializeComponent();
        _assets = assets;
        _settings = settings.Clone();
        _settings.Normalize();

        _timer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(16)
        };
        _timer.Tick += (_, _) => Tick();
        _timer.Start();

        SizeChanged += (_, _) => LayoutItems();
        MouseLeftButtonDown += (_, _) => DragMove();
    }

    public void ApplySettings(AppSettings settings)
    {
        _settings = settings.Clone();
        _settings.Normalize();
        LayoutItems();
    }

    public void ShowMessage(ChatMessage message)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.Invoke(() => ShowMessage(message));
            return;
        }

        var car = _assets.PickCar();
        var visual = CreateVisual(message, car);
        _items.Insert(0, visual);
        StageCanvas.Children.Add(visual.Root);
        LayoutItems();
    }

    protected override void OnClosed(EventArgs e)
    {
        _timer.Stop();
        base.OnClosed(e);
    }

    private RaceVisual CreateVisual(ChatMessage message, CarAsset car)
    {
        var root = new Grid
        {
            Width = BaseWidth,
            Height = BaseHeight,
            RenderTransformOrigin = new Point(0, 1)
        };

        root.Children.Add(new Image
        {
            Source = LoadBitmap(_assets.CommentFramePath),
            Width = 555,
            Height = 500,
            Stretch = Stretch.Uniform,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top
        });

        var carImage = new Image
        {
            Source = LoadBitmap(car.AbsolutePath),
            Width = 555,
            Stretch = Stretch.Uniform,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(486, 250, 0, 0)
        };
        root.Children.Add(carImage);

        var textPanel = new StackPanel
        {
            Width = 370,
            Margin = new Thickness(86, 278, 0, 0),
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top
        };

        var namePanel = new StackPanel
        {
            Orientation = Orientation.Horizontal
        };

        if (message.IsSuperChat)
        {
            namePanel.Children.Add(new Border
            {
                Background = new SolidColorBrush(Color.FromRgb(253, 230, 138)),
                CornerRadius = new CornerRadius(10),
                Padding = new Thickness(12, 4, 12, 5),
                Margin = new Thickness(0, 0, 10, 0),
                Child = new TextBlock
                {
                    Text = "SC",
                    Foreground = new SolidColorBrush(Color.FromRgb(22, 18, 10)),
                    FontSize = 30,
                    FontWeight = FontWeights.Black
                }
            });
        }

        namePanel.Children.Add(new TextBlock
        {
            Text = message.UserName,
            Foreground = Brushes.White,
            FontSize = 52,
            FontWeight = FontWeights.ExtraBold,
            TextTrimming = TextTrimming.CharacterEllipsis,
            MaxWidth = message.IsSuperChat ? 275 : 360,
            Effect = Shadow()
        });

        textPanel.Children.Add(namePanel);
        textPanel.Children.Add(new TextBlock
        {
            Text = message.Text,
            Foreground = Brushes.White,
            FontSize = 44,
            FontWeight = FontWeights.SemiBold,
            LineHeight = 50,
            TextWrapping = TextWrapping.Wrap,
            MaxHeight = 104,
            Margin = new Thickness(0, 24, 0, 0),
            Effect = Shadow()
        });
        root.Children.Add(textPanel);

        return new RaceVisual(root)
        {
            X = -BaseWidth,
            TargetX = 0
        };
    }

    private void LayoutItems()
    {
        var scale = ComputeScale();
        var itemWidth = BaseWidth * scale;
        var gap = BaseGap * scale;
        var visibleWidth = Math.Min(ActualWidth, _settings.MaxStageWidth);
        var capacity = Math.Max(_settings.MinVisibleItems, (int)Math.Floor(visibleWidth / Math.Max(1, itemWidth + gap)));

        while (_items.Count(item => !item.Leaving) > capacity)
        {
            var oldest = _items.LastOrDefault(item => !item.Leaving);
            if (oldest is null)
            {
                break;
            }

            oldest.Leaving = true;
            oldest.TargetX = ActualWidth + itemWidth;
        }

        var cursor = ActualWidth - itemWidth - 10;
        for (var index = _items.Count - 1; index >= 0; index--)
        {
            var item = _items[index];
            item.Root.RenderTransform = new ScaleTransform(scale, scale);
            Canvas.SetBottom(item.Root, 10);

            if (item.Leaving)
            {
                continue;
            }

            item.TargetX = Math.Max(-itemWidth, cursor);
            cursor -= itemWidth + gap;
        }
    }

    private double ComputeScale()
    {
        var heightScale = Math.Max(0.2, (ActualHeight - 10) / BaseHeight);
        var widthScale = Math.Max(
            0.2,
            ActualWidth / (_settings.MinVisibleItems * BaseWidth + (_settings.MinVisibleItems - 1) * BaseGap));
        return Math.Min(1, Math.Min(heightScale, widthScale));
    }

    private void Tick()
    {
        var pressure = Math.Min(1.0, _items.Count / Math.Max(1.0, _settings.MinVisibleItems + 3.0));
        for (var index = _items.Count - 1; index >= 0; index--)
        {
            var item = _items[index];
            var easing = item.Leaving ? 0.075 : 0.045 + pressure * 0.035;
            item.X += (item.TargetX - item.X) * easing;
            Canvas.SetLeft(item.Root, item.X);

            if (item.Leaving && item.X > ActualWidth + BaseWidth)
            {
                StageCanvas.Children.Remove(item.Root);
                _items.RemoveAt(index);
            }
        }
    }

    private static BitmapImage LoadBitmap(string path)
    {
        var image = new BitmapImage();
        image.BeginInit();
        image.CacheOption = BitmapCacheOption.OnLoad;
        image.UriSource = new Uri(path, UriKind.Absolute);
        image.EndInit();
        image.Freeze();
        return image;
    }

    private static System.Windows.Media.Effects.DropShadowEffect Shadow()
    {
        return new System.Windows.Media.Effects.DropShadowEffect
        {
            Color = Colors.Black,
            BlurRadius = 10,
            ShadowDepth = 3,
            Opacity = 0.55
        };
    }

    private sealed class RaceVisual(Grid root)
    {
        public Grid Root { get; } = root;
        public double X { get; set; }
        public double TargetX { get; set; }
        public bool Leaving { get; set; }
    }
}
