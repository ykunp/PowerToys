﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// Code forked from Betsegaw Tadele's https://github.com/betsegaw/windowwalker/
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace Microsoft.Plugin.WindowWalker.Components
{
    /// <summary>
    /// Responsible for searching and finding matches for the strings provided.
    /// Essentially the UI independent model of the application
    /// </summary>
    internal class SearchController
    {
        /// <summary>
        /// the current search text
        /// </summary>
        private string searchText;

        /// <summary>
        /// Open window search results
        /// </summary>
        private List<SearchResult> searchMatches;

        /// <summary>
        /// Singleton pattern
        /// </summary>
        private static SearchController instance;

        /// <summary>
        /// Gets or sets the current search text
        /// </summary>
        public string SearchText
        {
            get
            {
                return searchText;
            }

            set
            {
                // Using CurrentCulture since this is user facing
                searchText = value.ToLower(CultureInfo.CurrentCulture).Trim();
            }
        }

        /// <summary>
        /// Gets the open window search results
        /// </summary>
        public List<SearchResult> SearchMatches
        {
            get { return new List<SearchResult>(searchMatches).OrderByDescending(x => x.Score).ToList(); }
        }

        /// <summary>
        /// Gets singleton Pattern
        /// </summary>
        public static SearchController Instance
        {
            get
            {
                if (instance == null)
                {
                    instance = new SearchController();
                }

                return instance;
            }
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="SearchController"/> class.
        /// Initializes the search controller object
        /// </summary>
        private SearchController()
        {
            searchText = string.Empty;
        }

        /// <summary>
        /// Event handler for when the search text has been updated
        /// </summary>
        public void UpdateSearchText(string searchText)
        {
            SearchText = searchText;
            SyncOpenWindowsWithModel();
        }

        /// <summary>
        /// Syncs the open windows with the OpenWindows Model
        /// </summary>
        public void SyncOpenWindowsWithModel()
        {
            System.Diagnostics.Debug.Print("Syncing WindowSearch result with OpenWindows Model");

            List<Window> snapshotOfOpenWindows = OpenWindows.Instance.Windows;

            if (string.IsNullOrWhiteSpace(SearchText))
            {
                searchMatches = new List<SearchResult>();
            }
            else
            {
                searchMatches = FuzzySearchOpenWindows(snapshotOfOpenWindows);
            }
        }

        /// <summary>
        /// Search method that matches the title of windows with the user search text
        /// </summary>
        /// <param name="openWindows">what windows are open</param>
        /// <returns>Returns search results</returns>
        private List<SearchResult> FuzzySearchOpenWindows(List<Window> openWindows)
        {
            List<SearchResult> result = new List<SearchResult>();
            List<SearchString> searchStrings = new List<SearchString>();

            searchStrings.Add(new SearchString(searchText, SearchResult.SearchType.Fuzzy));

            foreach (var searchString in searchStrings)
            {
                foreach (var window in openWindows)
                {
                    var titleMatch = FuzzyMatching.SimpleMatch(window.Title, searchString.SearchText);
                    var processMatch = FuzzyMatching.SimpleMatch(window.ProcessInfo.Name, searchString.SearchText);

                    if ((titleMatch.Count != 0 || processMatch.Count != 0) &&
                                window.Title.Length != 0)
                    {
                        var temp = new SearchResult(window, titleMatch, processMatch, searchString.SearchType);
                        result.Add(temp);
                    }
                }
            }

            System.Diagnostics.Debug.Print("Found " + result.Count + " windows that match the search text");

            return result;
        }

        /// <summary>
        /// Event args for a window list update event
        /// </summary>
        public class SearchResultUpdateEventArgs : EventArgs
        {
        }
    }
}
