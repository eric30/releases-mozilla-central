/* -*- Mode: Java; c-basic-offset: 4; tab-width: 20; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko.home;

import org.mozilla.gecko.R;
import org.mozilla.gecko.db.BrowserContract.Bookmarks;
import org.mozilla.gecko.db.BrowserDB;
import org.mozilla.gecko.db.BrowserDB.URLColumns;
import org.mozilla.gecko.home.HomePager.OnUrlOpenListener;
import org.mozilla.gecko.util.GamepadUtils;
import org.mozilla.gecko.util.ThreadUtils;

import android.content.Context;
import android.database.Cursor;
import android.os.AsyncTask;
import android.util.AttributeSet;
import android.util.Pair;
import android.view.LayoutInflater;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.CursorAdapter;
import android.widget.ListView;

import java.util.LinkedList;

/**
 * A ListView of bookmarks.
 */
public class BookmarksListView extends HomeListView
                               implements AdapterView.OnItemClickListener{
    
    public static final String LOGTAG = "GeckoBookmarksListView";

    private int mFolderId = Bookmarks.FIXED_ROOT_ID;
    private String mFolderTitle = "";

    // A cursor based adapter.
    private BookmarksListAdapter mCursorAdapter = null;

    // A background task to query the db.
    private BookmarksQueryTask mQueryTask = null;

    // Folder title for the currently shown list of bookmarks.
    private BookmarkFolderView mFolderView;

    // Check for adding a header view, if needed.
    private boolean mHasFolderHeader = false;

    // The last motion event that was intercepted.
    private MotionEvent mMotionEvent;

    // The default touch slop.
    private int mTouchSlop;

    public BookmarksListView(Context context) {
        this(context, null);
    }

    public BookmarksListView(Context context, AttributeSet attrs) {
        this(context, attrs, android.R.attr.listViewStyle);
    }

    public BookmarksListView(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);

        // Scaled touch slop for this context.
        mTouchSlop = ViewConfiguration.get(context).getScaledTouchSlop();

        // Folder title view, is always in open state.
        mFolderView = (BookmarkFolderView) LayoutInflater.from(context).inflate(R.layout.bookmark_folder_row, null);
        mFolderView.open();
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();

        // Intialize the adapter.
        mCursorAdapter = new BookmarksListAdapter(getContext(), null);

        // We need to add the header before we set the adapter, hence make it null
        refreshListWithCursor(null);

        setOnItemClickListener(this);
        setOnKeyListener(GamepadUtils.getListItemClickDispatcher());

        mQueryTask = new BookmarksQueryTask();
        mQueryTask.execute();
    }

    @Override
    public void onDetachedFromWindow() {
        super.onDetachedFromWindow();

        // Can't use getters for adapter. It will create one if null.
        if (mCursorAdapter != null) {
            final Cursor cursor = mCursorAdapter.getCursor();
            mCursorAdapter = null;

            // Gingerbread locks the DB when closing a cursor, so do it in the background.
            ThreadUtils.postToBackgroundThread(new Runnable() {
                @Override
                public void run() {
                    if (cursor != null && !cursor.isClosed())
                        cursor.close();
                }
            });
        }
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent event) {
        switch(event.getAction() & MotionEvent.ACTION_MASK) {
            case MotionEvent.ACTION_DOWN: {
                // Store the event by obtaining a copy.
                mMotionEvent = MotionEvent.obtain(event);
                break;
            }

            case MotionEvent.ACTION_MOVE: {
                if ((mMotionEvent != null) &&
                    (Math.abs(event.getY() - mMotionEvent.getY()) > mTouchSlop)) {
                    // The user is scrolling. Pass the last event to this view,
                    // and make this view scroll.
                    onTouchEvent(mMotionEvent);
                    return true;
                }
                break;
            }

            default: {
                mMotionEvent = null;
                break;
            }
        }

        // Do default interception.
        return super.onInterceptTouchEvent(event);
    }

    @Override
    public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
        final ListView list = (ListView) parent;
        final int headerCount = list.getHeaderViewsCount();

        if (mHasFolderHeader) {
            // If we tap on the folder view (last of the header views),
            // move back to parent folder.
            if (position == headerCount - 1) {   
                mCursorAdapter.moveToParentFolder();
                return;
            }
        } else if (position < headerCount) {
            // The click is on a header, don't do anything.
            return;
        }

        final Cursor cursor = mCursorAdapter.getCursor();
        if (cursor == null) {
            return;
        }

        // Absolute position for the adapter.
        position -= headerCount;

        cursor.moveToPosition(position);

        int type = cursor.getInt(cursor.getColumnIndexOrThrow(Bookmarks.TYPE));
        if (type == Bookmarks.TYPE_FOLDER) {
            // If we're clicking on a folder, update adapter to move to that folder
            final int folderId = cursor.getInt(cursor.getColumnIndexOrThrow(Bookmarks._ID));
            final String folderTitle = mCursorAdapter.getFolderTitle(cursor);
            mCursorAdapter.moveToChildFolder(folderId, folderTitle);
        } else {
            // Otherwise, just open the URL
            final String url = cursor.getString(cursor.getColumnIndexOrThrow(URLColumns.URL));
            OnUrlOpenListener listener = getOnUrlOpenListener();
            if (listener != null) {
                listener.onUrlOpen(url);
            }
        }
    }

    private void refreshListWithCursor(Cursor cursor) {
        // We need to add the header before we set the adapter, hence making it null.
        setAdapter(null);

        // Add a header view based on the root folder.
        if (mFolderId == Bookmarks.FIXED_ROOT_ID) {
            if (mHasFolderHeader) {
                removeHeaderView(mFolderView);
                mHasFolderHeader = false;
            }
        } else {
            if (!mHasFolderHeader) {
                addHeaderView(mFolderView, null, true);
                mHasFolderHeader = true;
            }

            mFolderView.setText(mFolderTitle);
        }

        // This will update the cursorAdapter to use the new one if it already exists.
        mCursorAdapter.changeCursor(cursor);
        setAdapter(mCursorAdapter);

        // Reset the task.
        mQueryTask = null;
    }

    /**
     * Adapter to back the ListView with a list of bookmarks.
     */
    private class BookmarksListAdapter extends CursorAdapter {
        private static final int VIEW_TYPE_ITEM = 0;
        private static final int VIEW_TYPE_FOLDER = 1;

        private static final int VIEW_TYPE_COUNT = 2;

        // mParentStack holds folder id/title pairs that allow us to navigate
        // back up the folder heirarchy.
        private LinkedList<Pair<Integer, String>> mParentStack;

        public BookmarksListAdapter(Context context, Cursor cursor) {
            // Initializing with a null cursor.
            super(context, cursor);

            mParentStack = new LinkedList<Pair<Integer, String>>();

            // Add the root folder to the stack
            Pair<Integer, String> rootFolder = new Pair<Integer, String>(mFolderId, mFolderTitle);
            mParentStack.addFirst(rootFolder);
        }

        // Refresh the current folder by executing a new task.
        private void refreshCurrentFolder() {
            // Cancel any pre-existing async refresh tasks
            if (mQueryTask != null) {
                mQueryTask.cancel(false);
            }

            Pair<Integer, String> folderPair = mParentStack.getFirst();
            mFolderId = folderPair.first;
            mFolderTitle = folderPair.second;

            mQueryTask = new BookmarksQueryTask();
            mQueryTask.execute(new Integer(mFolderId));
        }

        /**
         * Moves to parent folder, if one exists.
         */
        public void moveToParentFolder() {
            // If we're already at the root, we can't move to a parent folder
            if (mParentStack.size() != 1) {
                mParentStack.removeFirst();
                refreshCurrentFolder();
            }
        }

        /**
         * Moves to child folder, given a folderId.
         *
         * @param folderId The id of the folder to show.
         * @param folderTitle The title of the folder to show.
         */
        public void moveToChildFolder(int folderId, String folderTitle) {
            Pair<Integer, String> folderPair = new Pair<Integer, String>(folderId, folderTitle);
            mParentStack.addFirst(folderPair);
            refreshCurrentFolder();
        }

        /**
         * {@inheritDoc}
         */
        @Override
        public int getItemViewType(int position) {
            Cursor c = getCursor();

            if (!c.moveToPosition(position)) {
                throw new IllegalStateException("Couldn't move cursor to position " + position);
            }

            return getItemViewType(c);
        }

        /**
         * Returns the type of the item at the given position in the cursor.
         *
         * @param cursor A cursor moved to the required position.
         * @return The type of the item.
         */
        public int getItemViewType(Cursor cursor) {
            if (cursor.getInt(cursor.getColumnIndexOrThrow(Bookmarks.TYPE)) == Bookmarks.TYPE_FOLDER) {
                return VIEW_TYPE_FOLDER;
            }

            // Default to returning normal item type.
            return VIEW_TYPE_ITEM;
        }

        /**
         * {@inheritDoc}
         */
        @Override
        public int getViewTypeCount() {
            return VIEW_TYPE_COUNT;
        }

        /**
         * Get the title of the folder given a cursor moved to the position.
         *
         * @param cursor A cursor moved to the required position.
         * @return The title of the folder at the position.
         */
        public String getFolderTitle(Cursor c) {
            String guid = c.getString(c.getColumnIndexOrThrow(Bookmarks.GUID));

            // If we don't have a special GUID, just return the folder title from the DB.
            if (guid == null || guid.length() == 12) {
                return c.getString(c.getColumnIndexOrThrow(Bookmarks.TITLE));
            }

            // Use localized strings for special folder names.
            if (guid.equals(Bookmarks.FAKE_DESKTOP_FOLDER_GUID)) {
                return getResources().getString(R.string.bookmarks_folder_desktop);
            } else if (guid.equals(Bookmarks.MENU_FOLDER_GUID)) {
                return getResources().getString(R.string.bookmarks_folder_menu);
            } else if (guid.equals(Bookmarks.TOOLBAR_FOLDER_GUID)) {
                return getResources().getString(R.string.bookmarks_folder_toolbar);
            } else if (guid.equals(Bookmarks.UNFILED_FOLDER_GUID)) {
                return getResources().getString(R.string.bookmarks_folder_unfiled);
            }

            // If for some reason we have a folder with a special GUID, but it's not one of
            // the special folders we expect in the UI, just return the title from the DB.
            return c.getString(c.getColumnIndexOrThrow(Bookmarks.TITLE));
        }

        @Override
        public void bindView(View view, Context context, Cursor cursor) {
            final int viewType = getItemViewType(cursor);

            if (viewType == VIEW_TYPE_ITEM) {
                TwoLinePageRow row = (TwoLinePageRow) view;
                row.updateFromCursor(cursor);
            } else {
                BookmarkFolderView row = (BookmarkFolderView) view;
                row.setText(getFolderTitle(cursor));
            }
        }

        @Override
        public View newView(Context context, Cursor cursor, ViewGroup parent) {
            final int viewType = getItemViewType(cursor);

            final int resId;
            if (viewType == VIEW_TYPE_ITEM) {
                resId = R.layout.home_item_row;
            } else {
                resId = R.layout.bookmark_folder_row;
            }

            return LayoutInflater.from(parent.getContext()).inflate(resId, null);
        }
    }

    /**
     * AsyncTask to query the DB for bookmarks.
     */
    private class BookmarksQueryTask extends AsyncTask<Integer, Void, Cursor> {
        @Override
        protected Cursor doInBackground(Integer... folderIds) {
            int folderId = Bookmarks.FIXED_ROOT_ID;

            if (folderIds.length != 0) {
                folderId = folderIds[0].intValue();
            }

            return BrowserDB.getBookmarksInFolder(getContext().getContentResolver(), folderId);
        }

        @Override
        protected void onPostExecute(final Cursor cursor) {
            refreshListWithCursor(cursor);
        }
    }
}
