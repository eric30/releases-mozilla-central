/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.preferences;

import org.mozilla.gecko.R;
import org.mozilla.gecko.PrefsHelper;

import android.preference.Preference;
import android.preference.PreferenceActivity;
import android.preference.PreferenceCategory;
import android.preference.PreferenceFragment;
import android.preference.PreferenceScreen;
import android.os.Bundle;
import android.util.Log;

/* A simple implementation of PreferenceFragment for large screen devices
 * This will strip category headers (so that they aren't shown to the user twice)
 * as well as initializing Gecko prefs when a fragment is shown.
*/
public class GeckoPreferenceFragment extends PreferenceFragment {

    private static final String LOGTAG = "GeckoPreferenceFragment";
    private int mPrefsRequestId = 0;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        String resourceName = getArguments().getString("resource");

        int res = 0;
        if (resourceName != null) {
            // Fetch resource id by resource name.
            res = getActivity().getResources().getIdentifier(resourceName,
                                                             "xml",
                                                             getActivity().getPackageName());
        }

        if (res == 0) {
            // The resource was invalid. Use the default resource.
            Log.e(LOGTAG, "Failed to find resource: " + resourceName + ". Displaying default settings.");

            boolean isMultiPane = ((PreferenceActivity) getActivity()).onIsMultiPane();
            res = isMultiPane ? R.xml.preferences_customize_tablet : R.xml.preferences;
        }
        addPreferencesFromResource(res);

        PreferenceScreen screen = getPreferenceScreen();
        setPreferenceScreen(screen);
        mPrefsRequestId = ((GeckoPreferences)getActivity()).setupPreferences(screen);
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        if (mPrefsRequestId > 0) {
            PrefsHelper.removeObserver(mPrefsRequestId);
        }
    }
}
