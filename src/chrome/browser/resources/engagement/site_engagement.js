// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

// Allow a function to be provided by tests, which will be called when
// the page has been populated with site engagement details.
var resolvePageIsPopulated = null;
var pageIsPopulatedPromise = new Promise((resolve, reject) => {
  resolvePageIsPopulated = resolve;
});

function whenPageIsPopulatedForTest() {
  return pageIsPopulatedPromise;
}

define('main', [
    'chrome/browser/engagement/site_engagement_details.mojom',
    'content/public/renderer/frame_interfaces',
], (siteEngagementMojom, frameInterfaces) => {
  return () => {
    var uiHandler = new siteEngagementMojom.SiteEngagementDetailsProviderPtr(
        frameInterfaces.getInterface(
            siteEngagementMojom.SiteEngagementDetailsProvider.name));

    var engagementTableBody = $('engagement-table-body');
    var updateInterval = null;
    var info = null;
    var sortKey = 'total_score';
    var sortReverse = true;

    // Set table header sort handlers.
    var engagementTableHeader = $('engagement-table-header');
    var headers = engagementTableHeader.children;
    for (var i = 0; i < headers.length; i++) {
      headers[i].addEventListener('click', (e) => {
        var newSortKey = e.target.getAttribute('sort-key');
        if (sortKey == newSortKey) {
          sortReverse = !sortReverse;
        } else {
          sortKey = newSortKey;
          sortReverse = false;
        }
        var oldSortColumn = document.querySelector('.sort-column');
        oldSortColumn.classList.remove('sort-column');
        e.target.classList.add('sort-column');
        if (sortReverse)
          e.target.setAttribute('sort-reverse', '');
        else
          e.target.removeAttribute('sort-reverse');
        renderTable();
      });
    }

    /**
     * Creates a single row in the engagement table.
     * @param {SiteEngagementDetails} info The info to create the row from.
     * @return {HTMLElement}
     */
    function createRow(info) {
      var originCell = createElementWithClassName('td', 'origin-cell');
      originCell.textContent = info.origin.url;

      var scoreInput = createElementWithClassName('input', 'score-input');
      scoreInput.addEventListener(
          'change', handleScoreChange.bind(undefined, info.origin));
      scoreInput.addEventListener('focus', disableAutoupdate);
      scoreInput.addEventListener('blur', enableAutoupdate);
      scoreInput.value = info.total_score;

      var scoreCell = createElementWithClassName('td', 'score-cell');
      scoreCell.appendChild(scoreInput);

      var engagementBar = createElementWithClassName('div', 'engagement-bar');
      engagementBar.style.width = (info.total_score * 4) + 'px';

      var engagementBarCell =
          createElementWithClassName('td', 'engagement-bar-cell');
      engagementBarCell.appendChild(engagementBar);

      var row = document.createElement('tr');
      row.appendChild(originCell);
      row.appendChild(scoreCell);
      row.appendChild(engagementBarCell);

      // Stores correspondent engagementBarCell to change it's length on
      // scoreChange event.
      scoreInput.barCellRef = engagementBar;
      return row;
    }

    function disableAutoupdate() {
      if (updateInterval)
        clearInterval(updateInterval);
      updateInterval = null;
    }

    function enableAutoupdate() {
      if (updateInterval)
        clearInterval(updateInterval);
      updateInterval = setInterval(updateEngagementTable, 5000);
    }

    /**
     * Sets the engagement score when a score input is changed.
     * Resets the length of engagement-bar-cell to match the new score.
     * Also resets the update interval.
     * @param {string} origin The origin of the engagement score to set.
     * @param {Event} e
     */
    function handleScoreChange(origin, e) {
      var scoreInput = e.target;
      uiHandler.setSiteEngagementScoreForUrl(origin, scoreInput.value);
      scoreInput.barCellRef.style.width = (scoreInput.value * 4) + 'px';
      scoreInput.blur();
      enableAutoupdate();
    }

    /**
     * Remove all rows from the engagement table.
     */
    function clearTable() {
      engagementTableBody.innerHTML = '';
    }

    /**
     * Sort the engagement info based on |sortKey| and |sortReverse|.
     */
    function sortInfo() {
      info.sort((a, b) => {
        return (sortReverse ? -1 : 1) *
               compareTableItem(sortKey, a, b);
      });
    }

    /**
     * Compares two SiteEngagementDetails objects based on |sortKey|.
     * @param {string} sortKey The name of the property to sort by.
     * @return {number} A negative number if |a| should be ordered before |b|, a
     * positive number otherwise.
     */
    function compareTableItem(sortKey, a, b) {
      var val1 = a[sortKey];
      var val2 = b[sortKey];

      // Compare the hosts of the origin ignoring schemes.
      if (sortKey == 'origin')
        return new URL(val1.url).host > new URL(val2.url).host ? 1 : -1;

      if (sortKey == 'total_score')
        return val1 - val2;

      assertNotReached('Unsupported sort key: ' + sortKey);
      return 0;
    }

    /**
     * Regenerates the engagement table from |info|.
     */
    function renderTable() {
      clearTable();
      sortInfo();
      // Round each score to 2 decimal places.
      info.forEach((info) => {
        info.total_score = Number(Math.round(info.total_score * 100) / 100);
        engagementTableBody.appendChild(createRow(info));
      });

      resolvePageIsPopulated();
    }

    /**
     * Retrieve site engagement info and render the engagement table.
     */
    function updateEngagementTable() {
      // Populate engagement table.
      uiHandler.getSiteEngagementDetails().then((response) => {
        info = response.info;
        renderTable(info);
      });
    };

    updateEngagementTable();
    enableAutoupdate();
  };
});
