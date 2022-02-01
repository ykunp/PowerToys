﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.CommandLine;
using System.CommandLine.Invocation;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using System.Reflection;
using System.Threading;
using System.Windows;
using Awake.Core;
using interop;
using ManagedCommon;
using Microsoft.PowerToys.Settings.UI.Library;
using NLog;

#pragma warning disable CS8602 // Dereference of a possibly null reference.
#pragma warning disable CS8603 // Possible null reference return.

namespace Awake
{
    internal class Program
    {
        private static Mutex? _mutex = null;
        private static FileSystemWatcher? _watcher = null;
        private static SettingsUtils? _settingsUtils = null;

        public static Mutex LockMutex { get => _mutex; set => _mutex = value; }

        private static Logger? _log;

#pragma warning disable CS8618 // Non-nullable field must contain a non-null value when exiting constructor. Consider declaring as nullable.
        private static ConsoleEventHandler _handler;
#pragma warning restore CS8618 // Non-nullable field must contain a non-null value when exiting constructor. Consider declaring as nullable.

        private static ManualResetEvent _exitSignal = new ManualResetEvent(false);

        private static int Main(string[] args)
        {
            // Log initialization needs to always happen before we test whether
            // only one instance of Awake is running.
            _log = LogManager.GetCurrentClassLogger();

            LockMutex = new Mutex(true, InternalConstants.AppName, out bool instantiated);

            if (!instantiated)
            {
                Exit(InternalConstants.AppName + " is already running! Exiting the application.", 1, true);
            }

            _settingsUtils = new SettingsUtils();

            _log.Info("Launching PowerToys Awake...");
            _log.Info(FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location).FileVersion);
            _log.Info($"OS: {Environment.OSVersion}");
            _log.Info($"OS Build: {APIHelper.GetOperatingSystemBuild()}");

            _log.Info("Parsing parameters...");

            Option<bool>? configOption = new Option<bool>(
                    aliases: new[] { "--use-pt-config", "-c" },
                    getDefaultValue: () => false,
                    description: "Specifies whether PowerToys Awake will be using the PowerToys configuration file for managing the state.")
            {
                Argument = new Argument<bool>(() => false)
                {
                    Arity = ArgumentArity.ZeroOrOne,
                },
            };

            configOption.Required = false;

            Option<bool>? displayOption = new Option<bool>(
                    aliases: new[] { "--display-on", "-d" },
                    getDefaultValue: () => true,
                    description: "Determines whether the display should be kept awake.")
            {
                Argument = new Argument<bool>(() => false)
                {
                    Arity = ArgumentArity.ZeroOrOne,
                },
            };

            displayOption.Required = false;

            Option<uint>? timeOption = new Option<uint>(
                    aliases: new[] { "--time-limit", "-t" },
                    getDefaultValue: () => 0,
                    description: "Determines the interval, in seconds, during which the computer is kept awake.")
            {
                Argument = new Argument<uint>(() => 0)
                {
                    Arity = ArgumentArity.ExactlyOne,
                },
            };

            timeOption.Required = false;

            Option<int>? pidOption = new Option<int>(
                    aliases: new[] { "--pid", "-p" },
                    getDefaultValue: () => 0,
                    description: "Bind the execution of PowerToys Awake to another process.")
            {
                Argument = new Argument<int>(() => 0)
                {
                    Arity = ArgumentArity.ZeroOrOne,
                },
            };

            pidOption.Required = false;

            RootCommand? rootCommand = new RootCommand
            {
                configOption,
                displayOption,
                timeOption,
                pidOption,
            };

            rootCommand.Description = InternalConstants.AppName;

            rootCommand.Handler = CommandHandler.Create<bool, bool, uint, int>(HandleCommandLineArguments);

            _log.Info("Parameter setup complete. Proceeding to the rest of the app initiation...");

            return rootCommand.InvokeAsync(args).Result;
        }

        private static bool ExitHandler(ControlType ctrlType)
        {
            _log.Info($"Exited through handler with control type: {ctrlType}");

            Exit("Exiting from the internal termination handler.", Environment.ExitCode);

            return false;
        }

        private static void Exit(string message, int exitCode, bool force = false)
        {
            _log.Info(message);

            APIHelper.SetNoKeepAwake();
            TrayHelper.ClearTray();

            // Because we are running a message loop for the tray, we can't just use Environment.Exit,
            // but have to make sure that we properly send the termination message.
            bool cwResult = System.Diagnostics.Process.GetCurrentProcess().CloseMainWindow();
            _log.Info($"Request to close main window status: {cwResult}");

            if (force)
            {
                Environment.Exit(exitCode);
            }
        }

        private static void HandleCommandLineArguments(bool usePtConfig, bool displayOn, uint timeLimit, int pid)
        {
            _handler += new ConsoleEventHandler(ExitHandler);
            APIHelper.SetConsoleControlHandler(_handler, true);

            if (pid == 0)
            {
                _log.Info("No PID specified. Allocating console...");
                APIHelper.AllocateConsole();
            }

            _log.Info($"The value for --use-pt-config is: {usePtConfig}");
            _log.Info($"The value for --display-on is: {displayOn}");
            _log.Info($"The value for --time-limit is: {timeLimit}");
            _log.Info($"The value for --pid is: {pid}");

            if (usePtConfig)
            {
                // Configuration file is used, therefore we disregard any other command-line parameter
                // and instead watch for changes in the file.
                try
                {
                    new Thread(() =>
                    {
                        EventWaitHandle? eventHandle = new EventWaitHandle(false, EventResetMode.AutoReset, Constants.AwakeExitEvent());
                        if (eventHandle.WaitOne())
                        {
                            Exit("Received a signal to end the process. Making sure we quit...", 0, true);
                        }
                    }).Start();

                    TrayHelper.InitializeTray(InternalConstants.FullAppName, new Icon("modules/Awake/Images/Awake.ico"));

                    string? settingsPath = _settingsUtils.GetSettingsFilePath(InternalConstants.AppName);
                    _log.Info($"Reading configuration file: {settingsPath}");

                    _watcher = new FileSystemWatcher
                    {
#pragma warning disable CS8601 // Possible null reference assignment.
                        Path = Path.GetDirectoryName(settingsPath),
#pragma warning restore CS8601 // Possible null reference assignment.
                        EnableRaisingEvents = true,
                        NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.CreationTime,
                        Filter = Path.GetFileName(settingsPath),
                    };

                    IObservable<System.Reactive.EventPattern<FileSystemEventArgs>>? changedObservable = Observable.FromEventPattern<FileSystemEventHandler, FileSystemEventArgs>(
                            h => _watcher.Changed += h,
                            h => _watcher.Changed -= h);

                    IObservable<System.Reactive.EventPattern<FileSystemEventArgs>>? createdObservable = Observable.FromEventPattern<FileSystemEventHandler, FileSystemEventArgs>(
                            cre => _watcher.Created += cre,
                            cre => _watcher.Created -= cre);

                    IObservable<System.Reactive.EventPattern<FileSystemEventArgs>>? mergedObservable = Observable.Merge(changedObservable, createdObservable);

                    mergedObservable.Throttle(TimeSpan.FromMilliseconds(25))
                        .SubscribeOn(TaskPoolScheduler.Default)
                        .Select(e => e.EventArgs)
                        .Subscribe(HandleAwakeConfigChange);

                    TrayHelper.SetTray(InternalConstants.FullAppName, new AwakeSettings());

                    // Initially the file might not be updated, so we need to start processing
                    // settings right away.
                    ProcessSettings();
                }
                catch (Exception ex)
                {
                    string? errorString = $"There was a problem with the configuration file. Make sure it exists.\n{ex.Message}";
                    _log.Info(errorString);
                    _log.Debug(errorString);
                }
            }
            else
            {
                AwakeMode mode = timeLimit <= 0 ? AwakeMode.INDEFINITE : AwakeMode.TIMED;

                if (mode == AwakeMode.INDEFINITE)
                {
                    SetupIndefiniteKeepAwake(displayOn);
                }
                else
                {
                    SetupTimedKeepAwake(timeLimit, displayOn);
                }
            }

            if (pid != 0)
            {
                RunnerHelper.WaitForPowerToysRunner(pid, () =>
                {
                    Exit("Terminating from PowerToys binding hook.", 0, true);
                });
            }

            _exitSignal.WaitOne();
        }

        private static void SetupIndefiniteKeepAwake(bool displayOn)
        {
            APIHelper.SetIndefiniteKeepAwake(LogCompletedKeepAwakeThread, LogUnexpectedOrCancelledKeepAwakeThreadCompletion, displayOn);
        }

        private static void HandleAwakeConfigChange(FileSystemEventArgs fileEvent)
        {
            _log.Info("Detected a settings file change. Updating configuration...");
            _log.Info("Resetting keep-awake to normal state due to settings change.");
            ProcessSettings();
        }

        private static void ProcessSettings()
        {
            try
            {
                AwakeSettings settings = _settingsUtils.GetSettings<AwakeSettings>(InternalConstants.AppName);

                if (settings != null)
                {
                    switch (settings.Properties.Mode)
                    {
                        case AwakeMode.PASSIVE:
                            {
                                SetupNoKeepAwake();
                                break;
                            }

                        case AwakeMode.INDEFINITE:
                            {
                                SetupIndefiniteKeepAwake(settings.Properties.KeepDisplayOn);
                                break;
                            }

                        case AwakeMode.TIMED:
                            {
                                uint computedTime = (settings.Properties.Hours * 60 * 60) + (settings.Properties.Minutes * 60);
                                SetupTimedKeepAwake(computedTime, settings.Properties.KeepDisplayOn);

                                break;
                            }

                        default:
                            {
                                string? errorMessage = "Unknown mode of operation. Check config file.";
                                _log.Info(errorMessage);
                                _log.Debug(errorMessage);
                                break;
                            }
                    }

                    TrayHelper.SetTray(InternalConstants.FullAppName, settings);
                }
                else
                {
                    string? errorMessage = "Settings are null.";
                    _log.Info(errorMessage);
                    _log.Debug(errorMessage);
                }
            }
            catch (Exception ex)
            {
                string? errorMessage = $"There was a problem reading the configuration file. Error: {ex.GetType()} {ex.Message}";
                _log.Info(errorMessage);
                _log.Debug(errorMessage);
            }
        }

        private static void SetupNoKeepAwake()
        {
            _log.Info($"Operating in passive mode (computer's standard power plan). No custom keep awake settings enabled.");

            APIHelper.SetNoKeepAwake();
        }

        private static void SetupTimedKeepAwake(uint time, bool displayOn)
        {
            _log.Info($"Timed keep-awake. Expected runtime: {time} seconds with display on setting set to {displayOn}.");

            APIHelper.SetTimedKeepAwake(time, LogCompletedKeepAwakeThread, LogUnexpectedOrCancelledKeepAwakeThreadCompletion, displayOn);
        }

        private static void LogUnexpectedOrCancelledKeepAwakeThreadCompletion()
        {
            string? errorMessage = "The keep-awake thread was terminated early.";
            _log.Info(errorMessage);
            _log.Debug(errorMessage);
        }

        private static void LogCompletedKeepAwakeThread(bool result)
        {
            _log.Info($"Exited keep-awake thread successfully: {result}");
        }
    }
}
