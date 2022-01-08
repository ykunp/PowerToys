// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using Microsoft.PowerToys.Settings.UI.Library;
using Microsoft.PowerToys.Settings.UI.Library.ViewModels;
using Windows.UI.Xaml.Controls;

namespace Microsoft.PowerToys.Settings.UI.Views
{
    public sealed partial class FancyZonesPage : Page
    {
        private FancyZonesViewModel ViewModel { get; set; }

        public FancyZonesPage()
        {
            InitializeComponent();
            var settingsUtils = new SettingsUtils();
            ViewModel = new FancyZonesViewModel(settingsUtils, SettingsRepository<GeneralSettings>.GetInstance(settingsUtils), SettingsRepository<FancyZonesSettings>.GetInstance(settingsUtils), ShellPage.SendDefaultIPCMessage);
            DataContext = ViewModel;
        }

        private void OpenColorsSettings_Click(object sender, Windows.UI.Xaml.RoutedEventArgs e)
        {
            Helpers.StartProcessHelper.Start(Helpers.StartProcessHelper.ColorsSettings);
        }
    }
}
