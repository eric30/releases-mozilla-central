/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.preferences;

import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.drawable.BitmapDrawable;
import android.os.Build;
import android.preference.Preference;
import android.text.SpannableString;
import android.util.Log;
import android.widget.TextView;
import android.widget.Toast;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.gecko.R;
import org.mozilla.gecko.gfx.BitmapUtils;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * Represents an element in the list of search engines on the preferences menu.
 */
public class SearchEnginePreference extends Preference {
    private static final String LOGTAG = "SearchEnginePreference";

    // Dimensions, in dp, of the icon to display for this engine.
    public static int sIconSize;

    // Indices in button array of the AlertDialog of the three buttons.
    public static final int INDEX_SET_DEFAULT_BUTTON = 0;
    public static final int INDEX_REMOVE_BUTTON = 1;

    // Cache label to avoid repeated use of the resource system.
    public final String LABEL_IS_DEFAULT;

    // Specifies if this engine is configured as the default search engine.
    private boolean mIsDefaultEngine;
    // Specifies if this engine is one of the ones bundled with the app, which cannot be deleted.
    private boolean mIsImmutableEngine;

    // Dialog element labels.
    private String[] mDialogItems;

    // The popup displayed when this element is tapped.
    private AlertDialog mDialog;

    private final SearchPreferenceCategory mParentCategory;

    /**
     * Create a preference object to represent a search engine that is attached to category
     * containingCategory.
     * @param context The activity context we operate under.
     * @param parentCategory The PreferenceCategory this object exists within.
     * @see this.setSearchEngine
     */
    public SearchEnginePreference(Context context, SearchPreferenceCategory parentCategory) {
        super(context);
        mParentCategory = parentCategory;

        Resources res = getContext().getResources();

        // Fetch the icon dimensions from the resource file.
        sIconSize = res.getDimensionPixelSize(R.dimen.searchpreferences_icon_size);
        setOnPreferenceClickListener(new OnPreferenceClickListener() {
            @Override
            public boolean onPreferenceClick(Preference preference) {
                SearchEnginePreference sPref = (SearchEnginePreference) preference;
                sPref.showDialog();

                return true;
            }
        });

        // Fetch this resource now, instead of every time we ever want to relabel a button.
        LABEL_IS_DEFAULT = res.getString(R.string.pref_search_default);

        // Set up default dialog items.
        mDialogItems = new String[] { res.getString(R.string.pref_search_set_default),
                                      res.getString(R.string.pref_search_remove) };
    }

    /**
     * Configure this Preference object from the Gecko search engine JSON object.
     * @param geckoEngineJSON The Gecko-formatted JSON object representing the search engine.
     * @throws JSONException If the JSONObject is invalid.
     */
    public void setSearchEngineFromJSON(JSONObject geckoEngineJSON) throws JSONException {
        final String engineName = geckoEngineJSON.getString("name");
        SpannableString titleSpannable = new SpannableString(engineName);
        mIsImmutableEngine = geckoEngineJSON.getBoolean("immutable");

        if (mIsImmutableEngine) {
            // Delete the "Remove" option from the menu.
            mDialogItems = new String[] { getContext().getResources().getString(R.string.pref_search_set_default) };
        }
        setTitle(titleSpannable);

        // setIcon is only available on Honeycomb and up.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
            // Create a drawable from the iconURI and assign it to this Preference for display.
            String iconURI = geckoEngineJSON.getString("iconURI");
            Bitmap iconBitmap = BitmapUtils.getBitmapFromDataURI(iconURI);
            Bitmap scaledIconBitmap = Bitmap.createScaledBitmap(iconBitmap, sIconSize, sIconSize, false);
            BitmapDrawable drawable = new BitmapDrawable(scaledIconBitmap);
            setIcon(drawable);
        }
    }

    /**
     * Set if this object's UI should show that this is the default engine.
     * @param isDefault Flag indicating if this represents the default engine.
     */
    public void setIsDefaultEngine(boolean isDefault) {
        mIsDefaultEngine = isDefault;
        if (isDefault) {
            setOrder(0);
            setSummary(LABEL_IS_DEFAULT);
        } else {
            setOrder(1);
            setSummary("");
        }
    }

    /**
     * Display the AlertDialog providing options to reconfigure this search engine. Sets an event
     * listener to disable buttons in the dialog as appropriate after they have been constructed by
     * Android.
     * @see this.configureShownDialog
     * @see this.hideDialog
     */
    public void showDialog() {
        // If we are the only engine left, then we are the default engine, and none of the options
        // on this menu can do anything.
        if (mParentCategory.getPreferenceCount() == 1) {
            ThreadUtils.postToUiThread(new Runnable() {
                @Override
                public void run() {
                    Toast.makeText(getContext(), R.string.pref_search_last_toast, Toast.LENGTH_SHORT).show();
                }
            });
            return;
        }

        // If we are both default and immutable, we have no enabled items to show on the menu - abort.
        if (mIsDefaultEngine && mIsImmutableEngine) {
            return;
        }

        final AlertDialog.Builder builder = new AlertDialog.Builder(getContext());
        builder.setTitle(getTitle().toString());
        builder.setItems(mDialogItems, new DialogInterface.OnClickListener() {
            // Forward the various events that we care about to the container class for handling.
            @Override
            public void onClick(DialogInterface dialog, int indexClicked) {
                hideDialog();
                switch (indexClicked) {
                    case INDEX_SET_DEFAULT_BUTTON:
                        mParentCategory.setDefault(SearchEnginePreference.this);
                        break;
                    case INDEX_REMOVE_BUTTON:
                        mParentCategory.uninstall(SearchEnginePreference.this);
                        break;
                    default:
                        Log.w(LOGTAG, "Selected index out of range.");
                        break;
                }
            }
        });

        // Copy the icon, if any, from this object to the prompt we produce.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
            builder.setIcon(getIcon());
        }

        // We have to construct the dialog itself on the UI thread.
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                mDialog = builder.create();
                mDialog.setOnShowListener(new DialogInterface.OnShowListener() {
                    // Called when the dialog is shown (so we're finally able to manipulate button enabledness).
                    @Override
                    public void onShow(DialogInterface dialog) {
                        configureShownDialog();
                    }
                });
                mDialog.show();
            }
        });
    }

    /**
     * Hide the dialog we previously created, if any.
     */
    public void hideDialog() {
        ThreadUtils.postToUiThread(new Runnable() {
            @Override
            public void run() {
                // Null check so we can chain engine-mutating methods up in SearchPreferenceCategory
                // without consequence.
                if (mDialog != null && mDialog.isShowing()) {
                    mDialog.dismiss();
                }
            }
        });
    }

    /**
     * Disables buttons in the shown AlertDialog as required. The button elements are not created
     * until after we call show, so this method has to be called from the onShowListener above.
     * @see this.showDialog
     */
    private void configureShownDialog() {
        // If we are the default engine, disable the "Set as default" button.
        TextView defaultButton = (TextView) mDialog.getListView().getChildAt(INDEX_SET_DEFAULT_BUTTON);
        // Disable "Set as default" button if we are already the default.
        if (mIsDefaultEngine) {
            defaultButton.setEnabled(false);
            // Failure to unregister this listener leads to tapping the button dismissing the dialog
            // without doing anything.
            defaultButton.setOnClickListener(null);
        }
    }
}
