/* -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */
<!--  to hide script contents from old browsers

var savedFlag = false;

function go( msg )
{
	netscape.security.PrivilegeManager.enablePrivilege( "AccountSetup" );

	if (parent.parent.globals.document.vars.editMode.value == "yes")
		return true;
	else
	{
		if ( msg=="Internet" )
		{
			if ( parent.parent.globals.document.setupPlugin.DialerConnect() == false )
				return false;
	
			// check browser version
			var theAgent=navigator.userAgent;
			var x=theAgent.indexOf("/");
			if (x>=0)	{
				theAgent=theAgent.substring(x+1,theAgent.length);
				x=theAgent.indexOf(".");
				if (x>0)	{
					theAgent=theAgent.substring(0,x);
					}			
				if (parseInt(theAgent)>=4)	{
				
					// Navigator 4.x specific features
				
					top.toolbar=true;
					top.menubar=true;
					top.locationbar=true;
					top.directory=true;
					top.statusbar=true;
					top.scrollbars=true;
					}
				}
	
			var theURL = parent.parent.globals.findVariable("HOME_URL");
			if (theURL == "" || theURL ==null)	{
				theURL = "http://home.netscape.com/";
				}
			parent.parent.location.replace(theURL);			// jumping to the URL
			}
		return(checkData());
		}
}

function checkData()
{
	return true;
}

function loadData()
{
	if ( parent.controls.generateControls )
		parent.controls.generateControls();
	if ( parent.parent.globals.document.vars.editMode.value != "yes" )
		saveAccountInfo( false );
}

function saveData()
{
}

function saveAccountInfo( promptFlag )
{
	savedFlag = parent.parent.globals.saveAccountInfo( promptFlag );
}

// end hiding contents from old browsers  -->
