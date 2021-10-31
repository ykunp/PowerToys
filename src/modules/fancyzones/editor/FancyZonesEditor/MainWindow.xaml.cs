// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Automation.Peers;
using System.Windows.Controls;
using System.Windows.Input;
using FancyZonesEditor.Logs;
using FancyZonesEditor.Models;
using FancyZonesEditor.Utils;
using Microsoft.PowerToys.Common.UI;
using ModernWpf.Controls;

namespace FancyZonesEditor
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private const int DefaultWrapPanelItemSize = 164;
        private const int SmallWrapPanelItemSize = 164;
        private const int MinimalForDefaultWrapPanelsHeight = 900;

        private readonly MainWindowSettingsModel _settings = ((App)Application.Current).MainWindowSettings;
        private LayoutModel _backup;

        private ContentDialog _openedDialog;
        private TextBlock _createLayoutAnnounce;

        public int WrapPanelItemSize { get; set; } = DefaultWrapPanelItemSize;

        public MainWindow(bool spanZonesAcrossMonitors, Rect workArea)
        {
            InitializeComponent();
            _createLayoutAnnounce = (TextBlock)FindName("LayoutCreationAnnounce");
            DataContext = _settings;

            KeyUp += MainWindow_KeyUp;
            PreviewKeyDown += MainWindow_PreviewKeyDown;

            if (spanZonesAcrossMonitors)
            {
                WindowStartupLocation = WindowStartupLocation.CenterScreen;
            }

            if (workArea.Height < MinimalForDefaultWrapPanelsHeight || App.Overlay.MultiMonitorMode)
            {
                WrapPanelItemSize = SmallWrapPanelItemSize;
            }

            MaxWidth = workArea.Width;
            MaxHeight = workArea.Height;
            SizeToContent = SizeToContent.WidthAndHeight;
        }

        public void Update()
        {
            DataContext = _settings;
        }

        private void MainWindow_KeyUp(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Escape)
            {
                CloseDialog(sender);
            }
        }

        // Prevent closing the dialog with enter
        private void MainWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter && _openedDialog != null && _openedDialog.IsVisible)
            {
                var source = e.OriginalSource as RadioButton;
                if (source != null && source.IsChecked != true)
                {
                    source.IsChecked = true;
                    e.Handled = true;
                }
            }
        }

        private void LayoutItem_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            CloseDialog(sender);
        }

        private void CloseDialog(object sender)
        {
            if (_openedDialog != null)
            {
                _openedDialog.Hide();
            }
            else
            {
                OnClosing(sender, null);
            }
        }

        private void DecrementZones_Click(object sender, RoutedEventArgs e)
        {
            var mainEditor = App.Overlay;
            if (!(mainEditor.CurrentDataContext is LayoutModel model))
            {
                return;
            }

            if (model.TemplateZoneCount > 1)
            {
                model.TemplateZoneCount--;
            }
        }

        private void IncrementZones_Click(object sender, RoutedEventArgs e)
        {
            var mainEditor = App.Overlay;
            if (!(mainEditor.CurrentDataContext is LayoutModel model))
            {
                return;
            }

            if (model.IsZoneAddingAllowed)
            {
                model.TemplateZoneCount++;
            }
        }

        private void LayoutItem_MouseEnter(object sender, MouseEventArgs e)
        {
            // Select(((Grid)sender).DataContext as LayoutModel);
        }

        private void LayoutItem_Focused(object sender, RoutedEventArgs e)
        {
            // Ignore focus on Edit button click
            if (e.Source is Button)
            {
                return;
            }

            Select(((Border)sender).DataContext as LayoutModel);
        }

        private void LayoutItem_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Return || e.Key == Key.Space)
            {
                // When certain layout item (template or custom) is focused through keyboard and user
                // presses Enter or Space key, layout will be applied.
                Apply();
            }
        }

        private void Select(LayoutModel newSelection)
        {
            _settings.SetSelectedModel(newSelection);
            App.Overlay.CurrentDataContext = newSelection;
        }

        private async void NewLayoutButton_Click(object sender, RoutedEventArgs e)
        {
            Logger.LogTrace();

            if (_openedDialog != null)
            {
                // another dialog already opened
                return;
            }

            string defaultNamePrefix = FancyZonesEditor.Properties.Resources.Default_Custom_Layout_Name;
            int maxCustomIndex = 0;
            foreach (LayoutModel customModel in MainWindowSettingsModel.CustomModels)
            {
                string name = customModel.Name;
                if (name.StartsWith(defaultNamePrefix))
                {
                    if (int.TryParse(name.Substring(defaultNamePrefix.Length), out int i))
                    {
                        if (maxCustomIndex < i)
                        {
                            maxCustomIndex = i;
                        }
                    }
                }
            }

            LayoutNameText.Text = defaultNamePrefix + " " + (++maxCustomIndex);
            GridLayoutRadioButton.IsChecked = true;
            GridLayoutRadioButton.Focus();
            await NewLayoutDialog.ShowAsync();
        }

        private void DuplicateLayout_Click(object sender, RoutedEventArgs e)
        {
            Logger.LogTrace();

            EditLayoutDialog.Hide();

            var mainEditor = App.Overlay;
            if (!(mainEditor.CurrentDataContext is LayoutModel model))
            {
                return;
            }

            model.IsSelected = false;

            // make a copy
            model = model.Clone();
            mainEditor.CurrentDataContext = model;

            string name = model.Name;
            var index = name.LastIndexOf('(');
            if (index != -1)
            {
                name = name.Remove(index);
                name = name.TrimEnd();
            }

            Announce(name, FancyZonesEditor.Properties.Resources.Layout_Creation_Announce);
            int maxCustomIndex = 0;
            foreach (LayoutModel customModel in MainWindowSettingsModel.CustomModels)
            {
                string customModelName = customModel.Name;
                if (customModelName.StartsWith(name))
                {
                    int openBraceIndex = customModelName.LastIndexOf('(');
                    int closeBraceIndex = customModelName.LastIndexOf(')');
                    if (openBraceIndex != -1 && closeBraceIndex != -1)
                    {
                        string indexSubstring = customModelName.Substring(openBraceIndex + 1, closeBraceIndex - openBraceIndex - 1);

                        if (int.TryParse(indexSubstring, out int i))
                        {
                            if (maxCustomIndex < i)
                            {
                                maxCustomIndex = i;
                            }
                        }
                    }
                }
            }

            model.Name = name + " (" + (++maxCustomIndex) + ')';

            model.Persist();

            App.Overlay.SetLayoutSettings(App.Overlay.Monitors[App.Overlay.CurrentDesktop], model);
            App.FancyZonesEditorIO.SerializeZoneSettings();
        }

        private void Announce(string name, string message)
        {
            if (AutomationPeer.ListenerExists(AutomationEvents.MenuOpened) && _createLayoutAnnounce != null)
            {
                var peer = UIElementAutomationPeer.FromElement(_createLayoutAnnounce);
                AutomationProperties.SetName(_createLayoutAnnounce, name + " " + message);
                peer?.RaiseAutomationEvent(AutomationEvents.MenuOpened);
            }
        }

        private void Apply()
        {
            Logger.LogTrace();
            var mainEditor = App.Overlay;
            if (mainEditor.CurrentDataContext is LayoutModel model)
            {
                _settings.SetAppliedModel(model);
                App.Overlay.SetLayoutSettings(App.Overlay.Monitors[App.Overlay.CurrentDesktop], model);
                App.FancyZonesEditorIO.SerializeZoneSettings();
            }
        }

        private void OnClosing(object sender, EventArgs e)
        {
            Logger.LogTrace();
            CancelLayoutChanges();

            App.FancyZonesEditorIO.SerializeZoneSettings();
            App.Overlay.CloseLayoutWindow();
            App.Current.Shutdown();
        }

        private void DeleteLayout_Click(object sender, RoutedEventArgs e)
        {
            Logger.LogTrace();
            EditLayoutDialog.Hide();
            DeleteLayout((FrameworkElement)sender);
        }

        private async void EditLayout_Click(object sender, RoutedEventArgs e)
        {
            Logger.LogTrace();

            // Avoid trying to open the same dialog twice.
            if (_openedDialog != null)
            {
                return;
            }

            var dataContext = ((FrameworkElement)sender).DataContext;
            Select((LayoutModel)dataContext);

            if (_settings.SelectedModel is GridLayoutModel grid)
            {
                _backup = new GridLayoutModel(grid);
            }
            else if (_settings.SelectedModel is CanvasLayoutModel canvas)
            {
                _backup = new CanvasLayoutModel(canvas);
            }

            await EditLayoutDialog.ShowAsync();
        }

        private void EditZones_Click(object sender, RoutedEventArgs e)
        {
            Logger.LogTrace();
            var dataContext = ((FrameworkElement)sender).DataContext;
            Select((LayoutModel)dataContext);
            EditLayoutDialog.Hide();
            var mainEditor = App.Overlay;
            if (!(mainEditor.CurrentDataContext is LayoutModel model))
            {
                return;
            }

            _settings.SetSelectedModel(model);

            Hide();
            mainEditor.OpenEditor(model);
        }

        private void ScrollViewer_PreviewMouseWheel(object sender, MouseWheelEventArgs e)
        {
            ScrollViewer scrollviewer = sender as ScrollViewer;
            if (e.Delta > 0)
            {
                scrollviewer.LineLeft();
            }
            else
            {
                scrollviewer.LineRight();
            }

            e.Handled = true;
        }

        private void NewLayoutDialog_PrimaryButtonClick(ModernWpf.Controls.ContentDialog sender, ModernWpf.Controls.ContentDialogButtonClickEventArgs args)
        {
            Logger.LogTrace();

            LayoutModel selectedLayoutModel;

            if (GridLayoutRadioButton.IsChecked == true)
            {
                GridLayoutModel gridModel = new GridLayoutModel(LayoutNameText.Text, LayoutType.Custom)
                {
                    Rows = 1,
                    RowPercents = new List<int>(1) { GridLayoutModel.GridMultiplier },
                };
                selectedLayoutModel = gridModel;
            }
            else
            {
                var area = App.Overlay.WorkArea;
                CanvasLayoutModel canvasModel = new CanvasLayoutModel(LayoutNameText.Text, LayoutType.Custom, (int)area.Width, (int)area.Height);
                canvasModel.AddZone();
                selectedLayoutModel = canvasModel;
            }

            selectedLayoutModel.InitTemplateZones();

            try
            {
                Hide();
            }
            catch
            {
                // See https://github.com/microsoft/PowerToys/issues/9396
                Hide();
            }

            App.Overlay.CurrentDataContext = selectedLayoutModel;
            App.Overlay.OpenEditor(selectedLayoutModel);
        }

        // This is required to fix a WPF rendering bug when using custom chrome
        private void OnContentRendered(object sender, EventArgs e)
        {
            InvalidateVisual();
        }

        // EditLayout: Cancel changes
        private void EditLayoutDialog_SecondaryButtonClick(ContentDialog sender, ContentDialogButtonClickEventArgs args)
        {
            CancelLayoutChanges();
            Select(_settings.AppliedModel);
        }

        // EditLayout: Save changes
        private void EditLayoutDialog_PrimaryButtonClick(ContentDialog sender, ContentDialogButtonClickEventArgs args)
        {
            Logger.LogTrace();

            var mainEditor = App.Overlay;
            if (!(mainEditor.CurrentDataContext is LayoutModel model))
            {
                return;
            }

            _backup = null;

            // update current settings
            if (model == _settings.AppliedModel)
            {
                App.Overlay.SetLayoutSettings(App.Overlay.Monitors[App.Overlay.CurrentDesktop], model);
            }

            App.FancyZonesEditorIO.SerializeZoneSettings();

            // reset selected model
            Select(_settings.AppliedModel);
        }

        private async void DeleteLayout(FrameworkElement element)
        {
            Logger.LogTrace();

            var dialog = new ContentDialog()
            {
                Title = Properties.Resources.Are_You_Sure,
                Content = Properties.Resources.Are_You_Sure_Description,
                PrimaryButtonText = Properties.Resources.Delete,
                SecondaryButtonText = Properties.Resources.Cancel,
            };

            Announce(FancyZonesEditor.Properties.Resources.Delete_Layout_Dialog_Announce, dialog.Content.ToString());
            var result = await dialog.ShowAsync();

            if (result == ContentDialogResult.Primary)
            {
                LayoutModel model = element.DataContext as LayoutModel;

                if (_backup != null && model.Guid == _backup.Guid)
                {
                    _backup = null;
                }

                if (model == _settings.AppliedModel)
                {
                    _settings.SetAppliedModel(_settings.BlankModel);
                    Select(_settings.BlankModel);
                }

                foreach (var monitor in App.Overlay.Monitors)
                {
                    if (monitor.Settings.ZonesetUuid == model.Uuid)
                    {
                        App.Overlay.SetLayoutSettings(monitor, _settings.BlankModel);
                    }
                }

                App.FancyZonesEditorIO.SerializeZoneSettings();
                model.Delete();
            }
        }

        private void Dialog_Opened(ContentDialog sender, ContentDialogOpenedEventArgs args)
        {
            Announce(sender.Name, FancyZonesEditor.Properties.Resources.Edit_Layout_Open_Announce);
            _openedDialog = sender;
        }

        private void Dialog_Closed(ContentDialog sender, ContentDialogClosedEventArgs args)
        {
            _openedDialog = null;
        }

        private void EditDialogNumberBox_KeyDown(object sender, KeyEventArgs e)
        {
            // Making sure that pressing Enter when changing values in a NumberBox will not close the edit dialog.
            if (e.Key == Key.Enter)
            {
                e.Handled = true;
            }
        }

        private void Layout_ItemClick(object sender, ItemClickEventArgs e)
        {
            Select(e.ClickedItem as LayoutModel);
            Apply();
        }

        private void Monitor_ItemClick(object sender, ItemClickEventArgs e)
        {
            monitorViewModel.SelectCommand.Execute(e.ClickedItem as MonitorInfoModel);
        }

        private void ComboBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter || e.Key == Key.Space)
            {
                e.Handled = true;
                ComboBox selectedComboBox = sender as ComboBox;
                if (!selectedComboBox.IsDropDownOpen)
                {
                    selectedComboBox.IsDropDownOpen = true;
                }
            }
        }

        private void TextBox_GotKeyboardFocus(object sender, RoutedEventArgs e)
        {
            TextBox tb = sender as TextBox;
            if (tb != null)
            {
                tb.SelectionStart = tb.Text.Length;
            }
        }

        private void CancelLayoutChanges()
        {
            if (_backup != null)
            {
                _settings.RestoreSelectedModel(_backup);
                _backup = null;
            }
        }

        private void NumberBox_Loaded(object sender, RoutedEventArgs e)
        {
            // The TextBox inside a NumberBox doesn't inherit the Automation Properties name, so we have to set it.
            var numberBox = sender as NumberBox;
            const string numberBoxTextBoxName = "InputBox"; // Text box template part name given by ModernWPF.
            numberBox.ApplyTemplate(); // Apply template to be able to change child's property.
            var numberBoxTextBox = numberBox.Template.FindName(numberBoxTextBoxName, numberBox) as TextBox;
            numberBoxTextBox.SetValue(AutomationProperties.NameProperty, numberBox.GetValue(AutomationProperties.NameProperty));
        }

        private void SettingsBtn_Click(object sender, RoutedEventArgs e)
        {
            SettingsDeepLink.OpenSettings(SettingsDeepLink.SettingsWindow.FancyZones);
        }
    }
}
