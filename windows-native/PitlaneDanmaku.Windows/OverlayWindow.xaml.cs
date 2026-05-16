using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using PitlaneDanmaku.Windows.Models;
using PitlaneDanmaku.Windows.Rendering;
using PitlaneDanmaku.Windows.Services;

namespace PitlaneDanmaku.Windows;

public partial class OverlayWindow : Window
{
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
            Width = OverlayLayout.BaseWidth,
            Height = OverlayLayout.BaseHeight,
            RenderTransformOrigin = new Point(0, 1)
        };

        root.Children.Add(new Image
        {
            Source = LoadBitmap(_assets.CommentFramePath),
            Width = OverlayLayout.FrameWidth,
            Height = OverlayLayout.FrameHeight,
            Stretch = Stretch.Uniform,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top
        });

        var carImage = new Image
        {
            Source = LoadBitmap(car.AbsolutePath),
            Width = car.Width,
            Height = car.Height,
            Stretch = Stretch.Uniform,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            Margin = new Thickness(OverlayLayout.CarLeft, OverlayLayout.BaseHeight - car.Height - OverlayLayout.CarBottom, 0, 0)
        };
        root.Children.Add(carImage);

        var textPanel = new StackPanel
        {
            Width = OverlayLayout.TextWidth,
            Margin = new Thickness(OverlayLayout.TextLeft, OverlayLayout.TextTop, 0, 0),
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
                    FontFamily = EmbeddedFonts.HarmonySansSc,
                    Foreground = new SolidColorBrush(Color.FromRgb(22, 18, 10)),
                    FontSize = 30,
                    FontWeight = FontWeights.Black
                }
            });
        }

        namePanel.Children.Add(new TextBlock
        {
            Text = message.UserName,
            FontFamily = EmbeddedFonts.HarmonySansSc,
            Foreground = Brushes.White,
            FontSize = OverlayLayout.NameFontSize,
            FontWeight = FontWeights.Black,
            TextTrimming = TextTrimming.CharacterEllipsis,
            MaxWidth = message.IsSuperChat ? OverlayLayout.SuperChatNameWidth : OverlayLayout.NameWidth,
            Effect = Shadow()
        });

        textPanel.Children.Add(namePanel);
        textPanel.Children.Add(new TextBlock
        {
            Text = message.Text,
            FontFamily = EmbeddedFonts.HarmonySansSc,
            Foreground = Brushes.White,
            FontSize = OverlayLayout.MessageFontSize,
            FontWeight = FontWeights.SemiBold,
            LineHeight = OverlayLayout.MessageLineHeight,
            TextWrapping = TextWrapping.Wrap,
            MaxHeight = OverlayLayout.MessageMaxHeight,
            Margin = new Thickness(0, OverlayLayout.MessageTopMargin, 0, 0),
            Effect = Shadow()
        });
        root.Children.Add(textPanel);

        return new RaceVisual(root)
        {
            X = -OverlayLayout.BaseWidth,
            TargetX = 0
        };
    }

    private void LayoutItems()
    {
        var scale = ComputeScale();
        var itemWidth = OverlayLayout.BaseWidth * scale;
        var gap = OverlayLayout.BaseGap * scale;
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
        var heightScale = Math.Max(0.2, (ActualHeight - 10) / OverlayLayout.BaseHeight);
        var widthScale = Math.Max(
            0.2,
            ActualWidth / (_settings.MinVisibleItems * OverlayLayout.BaseWidth + (_settings.MinVisibleItems - 1) * OverlayLayout.BaseGap));
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

            if (item.Leaving && item.X > ActualWidth + OverlayLayout.BaseWidth)
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
