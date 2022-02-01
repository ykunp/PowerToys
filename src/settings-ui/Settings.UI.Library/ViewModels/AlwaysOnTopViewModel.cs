﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Runtime.CompilerServices;
using Microsoft.PowerToys.Settings.UI.Library.Helpers;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;

namespace Microsoft.PowerToys.Settings.UI.Library.ViewModels
{
    public class AlwaysOnTopViewModel : Observable
    {
        private ISettingsUtils SettingsUtils { get; set; }

        private GeneralSettings GeneralSettingsConfig { get; set; }

        private AlwaysOnTopSettings Settings { get; set; }

        private Func<string, int> SendConfigMSG { get; }

        public AlwaysOnTopViewModel(ISettingsUtils settingsUtils, ISettingsRepository<GeneralSettings> settingsRepository, ISettingsRepository<AlwaysOnTopSettings> moduleSettingsRepository, Func<string, int> ipcMSGCallBackFunc)
        {
            if (settingsUtils == null)
            {
                throw new ArgumentNullException(nameof(settingsUtils));
            }

            SettingsUtils = settingsUtils;

            // To obtain the general settings configurations of PowerToys Settings.
            if (settingsRepository == null)
            {
                throw new ArgumentNullException(nameof(settingsRepository));
            }

            GeneralSettingsConfig = settingsRepository.SettingsConfig;

            // To obtain the settings configurations of AlwaysOnTop.
            if (moduleSettingsRepository == null)
            {
                throw new ArgumentNullException(nameof(moduleSettingsRepository));
            }

            Settings = moduleSettingsRepository.SettingsConfig;

            _isEnabled = GeneralSettingsConfig.Enabled.AlwaysOnTop;
            _hotkey = Settings.Properties.Hotkey.Value;
            _frameEnabled = Settings.Properties.FrameEnabled.Value;
            _frameThickness = Settings.Properties.FrameThickness.Value;
            _frameColor = Settings.Properties.FrameColor.Value;
            _soundEnabled = Settings.Properties.SoundEnabled.Value;
            _doNotActivateOnGameMode = Settings.Properties.DoNotActivateOnGameMode.Value;
            _excludedApps = Settings.Properties.ExcludedApps.Value;
            _frameAccentColor = Settings.Properties.FrameAccentColor.Value;

            // set the callback functions value to hangle outgoing IPC message.
            SendConfigMSG = ipcMSGCallBackFunc;
        }

        public bool IsEnabled
        {
            get => _isEnabled;

            set
            {
                if (value != _isEnabled)
                {
                    _isEnabled = value;

                    // Set the status in the general settings configuration
                    GeneralSettingsConfig.Enabled.AlwaysOnTop = value;
                    OutGoingGeneralSettings snd = new OutGoingGeneralSettings(GeneralSettingsConfig);

                    SendConfigMSG(snd.ToString());
                    OnPropertyChanged(nameof(IsEnabled));
                }
            }
        }

        public HotkeySettings Hotkey
        {
            get => _hotkey;

            set
            {
                if (value != _hotkey)
                {
                    if (value == null || value.IsEmpty())
                    {
                        _hotkey = AlwaysOnTopProperties.DefaultHotkeyValue;
                    }
                    else
                    {
                        _hotkey = value;
                    }

                    Settings.Properties.Hotkey.Value = _hotkey;
                    NotifyPropertyChanged();
                }
            }
        }

        public bool FrameEnabled
        {
            get => _frameEnabled;

            set
            {
                if (value != _frameEnabled)
                {
                    _frameEnabled = value;
                    Settings.Properties.FrameEnabled.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public int FrameThickness
        {
            get => _frameThickness;

            set
            {
                if (value != _frameThickness)
                {
                    _frameThickness = value;
                    Settings.Properties.FrameThickness.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public string FrameColor
        {
            get => _frameColor;

            set
            {
                if (value != _frameColor)
                {
                    _frameColor = value;
                    Settings.Properties.FrameColor.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public bool SoundEnabled
        {
            get => _soundEnabled;

            set
            {
                if (value != _soundEnabled)
                {
                    _soundEnabled = value;
                    Settings.Properties.SoundEnabled.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public bool DoNotActivateOnGameMode
        {
            get => _doNotActivateOnGameMode;

            set
            {
                if (value != _doNotActivateOnGameMode)
                {
                    _doNotActivateOnGameMode = value;
                    Settings.Properties.DoNotActivateOnGameMode.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public string ExcludedApps
        {
            get => _excludedApps;

            set
            {
                if (value != _excludedApps)
                {
                    _excludedApps = value;
                    Settings.Properties.ExcludedApps.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public bool FrameAccentColor
        {
            get => _frameAccentColor;

            set
            {
                if (value != _frameAccentColor)
                {
                    _frameAccentColor = value;
                    Settings.Properties.FrameAccentColor.Value = value;
                    NotifyPropertyChanged();
                }
            }
        }

        public void NotifyPropertyChanged([CallerMemberName] string propertyName = null)
        {
            OnPropertyChanged(propertyName);
            SettingsUtils.SaveSettings(Settings.ToJsonString(), AlwaysOnTopSettings.ModuleName);
        }

        private bool _isEnabled;
        private HotkeySettings _hotkey;
        private bool _frameEnabled;
        private int _frameThickness;
        private string _frameColor;
        private bool _soundEnabled;
        private bool _doNotActivateOnGameMode;
        private string _excludedApps;
        private bool _frameAccentColor;
    }
}
