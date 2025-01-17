/*©agpl*************************************************************************
*                                                                              *
* This file is part of FRIEND UNIFYING PLATFORM.                               *
* Copyright (c) Friend Software Labs AS. All rights reserved.                  *
*                                                                              *
* Licensed under the Source EULA. Please refer to the copy of the GNU Affero   *
* General Public License, found in the file license_agpl.txt.                  *
*                                                                              *
*****************************************************************************©*/

/** @file
 *
 * File browser class
 * 
 *
 * @author HT (Hogne Titlestad)
 */

Friend = window.Friend || {};

// File browser ----------------------------------------------------------------

Friend.FileBrowserEntry = function()
{
};

/*
	Filebrowser class - creates a recursive file browser!
	
	flags:
	{
		displayFiles: true | false // File extension filter
		filedialog: true | false   // Is the browser used in a file dialog?
		justPaths: true | false    // Just show the directories?
		path: true | false         // Target path to focus on at start
		bookmarks: true | false    // Show the bookmarks pane?
		rootPath: true | false     // The root from which to display structures
	}
	
	callbacks:
	{
		void checkFile( filepath, fileextension )
		void loadFile( filepath )
		bool permitFiletype( filepath )
	}
	
	// flags.path is the target path in a query
	// rootPath is where to start listing files from
*/


Friend.FileBrowser = function( initElement, flags, callbacks )
{
	var self = this;
	this.dom = initElement;
	this.dom.classList.add( 'FileBrowser' );
	this.rootPath = 'Mountlist:'; // The current root path
	this.callbacks = callbacks;
	
	self.flags = { displayFiles: false, filedialog: false, justPaths: false, path: self.rootPath, bookmarks: true, rootPath: false };
	if( flags )
	{
		for( var a in flags )
			self.flags[ a ] = flags[ a ];
	}
	if( this.flags.rootPath )
		this.rootPath = this.flags.rootPath;
	
	// If we don't have a rootPath the default is Mountlist:
	if( !this.rootPath ) this.rootPath = 'Mountlist:';
	
	// Clicking the pane
	this.dom.onclick = function( e )
	{
		var t = e.target ? e.target : e.srcElement;
		if( t == this )
		{
			var cb = function()
			{
				if( t )
				{
					self.callbacks.folderOpen( self.rootPath, e );
				}
				t = null;
			};
			// Can't set icon listing path to mountlist..
			if( self.rootPath != 'Mountlist:' )
			{
				self.setPath( self.rootPath, cb );
			}
		}
		return cancelBubble( e );
	}
};
Friend.FileBrowser.prototype.clear = function()
{
	this.dom.innerHTML = '';
	this.headerDisks = false;
	this.bookmarksHeader = false;
}
Friend.FileBrowser.prototype.render = function()
{
	var self = this;
	
	this.refresh();
};
Friend.FileBrowser.prototype.drop = function( elements, e, win )
{
	var drop = 0;
	var self = this;
	// Only if we have bookmarks
	if( self.flags.bookmarks )
	{
		// Element was dropped here
		for( var a = 0; a < elements.length; a++ )
		{
			if( elements[a].fileInfo.Type == 'Directory' )
			{
				if( elements[a].fileInfo.Path.substr( elements[a].fileInfo.Path - 1, 1 ) != ':' )
				{
					var m = new Module( 'system' );
					m.onExecuted = function( e, d )
					{
						if( e == 'ok' )
						{
							self.clear();
							self.refresh( 'Mountlist:' );
						}
					}
					m.execute( 'addbookmark', { path: elements[a].fileInfo.Path, name: elements[a].fileInfo.Filename } );
					drop++;
				}
			}
		}
		if( win && drop == 0 )
		{
			if( win.refresh )
			{
				win.refresh();
			}
		}
	}
	return drop;
};

// Set an active path
// Supported flags ( { lockHistory: true|false } )
Friend.FileBrowser.prototype.setPath = function( target, cbk, tempFlags )
{
	// Already set
	if( this.flags.path && this.flags.path == target ) 
	{
		return;
	}
	
	this.tempFlags = false;
	this.flags.path = target; // This is the current target path..
	if( tempFlags ) this.tempFlags = tempFlags;
	this.refresh( this.rootPath, this.dom, cbk, 0 );
}

Friend.FileBrowser.prototype.rollOver = function( elements )
{
	// Do some user feedback later
};
Friend.FileBrowser.prototype.refresh = function( path, rootElement, callback, depth, flags )
{
	var self = this;
	
	if( !rootElement ) rootElement = this.dom;
	if( !callback ) callback = false;
	if( !path ) path = this.rootPath; // Use the rootpath
	if( !depth ) depth = 1;

	// Fix column problem
	if ( path.indexOf( ':' ) < 0 )
		path += ':';

	if( !this.headerDisks )
	{
		this.headerDisks = document.createElement( 'div' );
		this.headerDisks.innerHTML = '<p class="Layout BorderBottom PaddingBottom"><strong>' + i18n( 'i18n_your_devices' ) + ':</strong></p>';
		rootElement.appendChild( this.headerDisks );
	}
	
	// What are we looking for at this level?
	// Keeps the whole target path, but searches on each level recursively..
	var targetPath = false;
	if( this.flags.path )
	{
		targetPath = this.flags.path;
		/*var b = this.flags.path.split( ':' ).join( '/' ).split( '/' );
		b.pop();
		targetPath = '';
		
		for( var a = 0; a < depth; a++ )
		{
			if( b[a] )
			{
				targetPath += b[a] + ( a == 0 ? ':' : '/' );
			}
		}
		console.log( 'Looking: ' + targetPath + ' (' + this.flags.path + ') - ' + depth + ' ' + path );*/
	}
	
	function createOnclickAction( ele, ppath, type, depth )
	{
		ele.onclick = function( e )
		{
			// Real click removes temp flags
			if( e && e.button >= 0 )
				self.tempFlags = false;
				
			if( !ppath ) 
			{
				return cancelBubble( e );
			}
			if ( ppath.indexOf( ':' ) < 0 )
				ppath += ':';

			// Real click or entering target path
			var doClick = ( ppath == self.flags.path ) || ( e && e.button >= 0 );

			if( type == 'File' )
			{
				var eles = self.dom.getElementsByTagName( 'div' );
				for( var a = 0; a < eles.length; a++ )
				{
					eles[a].classList.remove( 'Active' );
				}
				var nam = ele.getElementsByClassName( 'Name' );
				if( nam.length )
				{
					nam[0].classList.add( 'Active' );
				}
				
				var treated = false;
				var ext = ppath.split( '.' );
				if( ext && ext.length )
				{
					ext = ext[ ext.length - 1 ].toLowerCase();
					if( self.callbacks.checkFile )
					{
						self.callbacks.checkFile( ppath, ext );
						treated = true;
						return cancelBubble( e );
					}
				}
				if( !treated && self.callbacks.loadFile )
				{
					self.callbacks.loadFile( ppath, e, self.tempFlags );
				}
			}
			else if( type == 'RootDirectory' )
			{
				// Are we in a file dialog?
				if( isMobile && ( self.flags.filedialog || self.flags.justPaths ) )
				{
					self.callbacks.folderOpen( ppath, e, self.tempFlags );
					return cancelBubble( e );
				}
				
				if( doClick )
				{
					if( self.callbacks && self.callbacks.folderOpen )
					{
						self.callbacks.folderOpen( ppath, e, self.tempFlags );
					}
				}
				
				// Set this to active
				// Set this to active
				var eles = self.dom.getElementsByTagName( 'div' );
				for( var a = 0; a < eles.length; a++ )
				{
					eles[a].classList.remove( 'Active' );
				}
				var nam = ele.getElementsByClassName( 'Name' );
				if( nam.length )
				{
					nam[0].classList.add( 'Active' );
				}
			}
			else if( type == 'Directory' || type == 'volume' )
			{
				// Are we in a file dialog?
				if( isMobile && ( self.flags.filedialog || self.flags.justPaths ) )
				{
					self.callbacks.folderOpen( ppath, e, self.tempFlags );
					return cancelBubble( e );
				}
				
				var nam = ele.getElementsByClassName( 'Name' );
				
				// Normal operation
				if( !this.classList.contains( 'Open' ) || ( e && e.mode == 'open' ) )
				{
					var subitems = ele.getElementsByClassName( 'SubItems' );
					if( subitems.length )
					{
						this.classList.add( 'Open' );

						// Only refresh at final destination
						if( doClick )
						{
							self.refresh( ppath, subitems[0], callback, depth );
							if( self.callbacks && self.callbacks.folderOpen )
							{
								self.callbacks.folderOpen( ppath, e, self.tempFlags );
							}
						}
						if( nam.length )
						{
							nam[0].classList.add( 'Open' );
						}
					}
				}
				// Only close folders if they are active and clicked
				else if( nam.length && e && ( ( !isMobile && e.button >= 0 ) || ( isMobile && doClick ) ) )
				{
					// Only close active
					if( nam[0].classList.contains( 'Active' ) )
					{
						this.classList.remove( 'Open' );
						nam[0].classList.remove( 'Open' );
					
						if( self.callbacks && self.callbacks.folderClose )
						{
							self.callbacks.folderClose( ppath, e, self.tempFlags );
						}
					}
					// Again clicking (like open...)
					else if( self.callbacks && self.callbacks.folderOpen )
					{
						self.callbacks.folderOpen( ppath, e, self.tempFlags );
					}
				}
				
				// Set this to active
				var eles = self.dom.getElementsByTagName( 'div' );
				for( var a = 0; a < eles.length; a++ )
				{
					eles[a].classList.remove( 'Active' );
				}
				var nam = ele.getElementsByClassName( 'Name' );
				if( nam.length )
				{
					nam[0].classList.add( 'Active' );
				}
				
				var fnam = ppath.split( ':' )[1];
				if( fnam.indexOf( '/' ) > 0 )
					fnam = fnam.split( '/' ).pop();
			}
			return cancelBubble( e );
		}
		ele.oncontextmenu = function( e )
		{
			if( isMobile ) return;
			
			var men = cmd = '';
			var cf = false; // create file
			if( type == 'File' )
			{
				men = 'i18n_open_file';
				cmd = 'menuloadfile';
			}
			else if( ele.classList.contains( 'Open' ) )
			{
				// Only show this if it is possible
				if( i18n( 'i18n_create_file' ) != 'i18n_create_file' )
				{
					if( type == 'Volume' || type == 'Directory' )
					{
						cf = {
							name: i18n( 'i18n_create_file' ),
							command: 'createfile',
							data: { path: ppath }
						};
					}
				}
			}
			var menu = [];
			if( men && cmd )
			{
				menu.push( {
					name: i18n( men ),
					command: cmd,
					data: { path: ppath }
				} );
			}
			if( cmd != 'menuloadfile' )
			{
				menu.push( {
					name: i18n( 'i18n_create_folder' ),
					command: 'createdirectory',
					data: { path: ppath }
				} );
			}
			if( cf ) menu.push( cf );
			if( window.ShowContextMenu )
			{
				ShowContextMenu( i18n( 'i18n_file_menu' ), menu );
			}
			else if( window.Workspace )
				Workspace.showContextMenu( menu, e );
			return cancelBubble( e );
		}
	}

	// A click element for incoming path
	var clickElement = null;

	// Just get a list of disks
	if( path == 'Mountlist:' )
	{
		var func = function( flags, cb )
		{
			// For Workspace scope
			if( window.Workspace )
			{
				Friend.DOS.getDisks( flags, function( response, msg )
				{
					cb( response ? {
						list: msg
					} : false );
				} );
			}
			// For application scope
			else
			{
				Friend.DOS.getDisks( flags, cb );
			}
		}
		// Check from off mountlist
		func( { sort: true }, function( msg )
		{	
			if( !msg || !msg.list ) return;
			
			if( callback ) callback();
			
			function done()
			{
				// Get existing
				var eles = rootElement.childNodes;
								
				var found = [];
				var foundElements = [];
				var foundStructures = [];
				var removers = [];
				for( var a = 0; a < eles.length; a++ )
				{
					var elFound = false;
					for( var b = 0; b < msg.list.length; b++ )
					{
						if( msg.list[ b ].Volume == 'System:' ) continue;
						
						if( eles[a].id == 'diskitem_' + msg.list[b].Title )
						{
							createOnclickAction( eles[a], msg.list[b].Volume, 'volume', depth + 1 );
							
							// Don't add twice
							if( !found.find( function( ele ){ ele == msg.list[b].Title } ) )
							{
								found.push( msg.list[b].Title ); // Found item titles
								foundElements.push( eles[a] ); // Found dom nodes
								foundStructures.push( msg.list[ b ] ); // Found dos structures
							}
							elFound = true;
						}
					}
					// Deleted element
					if( !elFound && eles[a] != self.headerDisks && eles[a] != self.bookmarksHeader )
					{
						removers.push( eles[a] );
					}
				}
				if( removers.length )
				{
					for( var a = 0; a < removers.length; a++ )
					{
						rootElement.removeChild( removers[a] );
					}
					delete removers;
				}
				
				// Iterate through the resulting list
				for( var a = 0; a < msg.list.length; a++ )
				{
					// Skip system drive
					if( msg.list[a].Volume == 'System:' ) continue;
					
					// Add the bookmark header if it doesn't exist
					if( self.flags.bookmarks && msg.list[a].Type && msg.list[a].Type == 'header' && !self.bookmarksHeader )
					{
						var d = document.createElement( 'div' );
						self.bookmarksHeader = d;
						d.innerHTML = '<p class="Layout BorderBottom PaddingTop BorderTop PaddingBottom"><strong>' + i18n( 'i18n_bookmarks' ) + ':</strong></p>';
						rootElement.appendChild( d );
						continue;
					}
					
					// Check if this item already exists
					var foundItem = foundStructure = false;
					for( var b = 0; b < found.length; b++ )
					{
						if( found[b] == msg.list[a].Title || msg.list[a].Type == 'header' )
						{
							foundItem = foundElements[ b ];
							foundStructure = foundStructures[ b ];
							break;
						}
					}
					
					// Didn't already exist, make it
					if( !foundItem )
					{
						var d = document.createElement( 'div' );
						d.className = 'DiskItem';
						d.id = 'diskitem_' + msg.list[a].Title;
						d.path = msg.list[a].Volume;
						var nm = document.createElement( 'div' );
						nm.style.paddingLeft = ( depth << 3 ) + 'px'; // * 8
						nm.className = 'Name IconSmall IconDisk';
						nm.innerHTML = '<span> ' + msg.list[a].Title + '</span>';
						
						// We have an incoming path
						if( !clickElement && self.flags.path && targetPath == d.path )
						{
							clickElement = d;
						}				
						
						if( Friend.dosDrivers && !( msg.list[a].Type && msg.list[a].Type == 'bookmark' ) )
						{
							var driver = msg.list[a].Driver;
							
							// Find correct image
							var img = '/iconthemes/friendup15/DriveLabels/FriendDisk.svg';
							if( Friend.dosDrivers[ driver ] && Friend.dosDrivers[ driver ].iconLabel )
								img = 'data:image/svg+xml;base64,' + Friend.dosDrivers[ driver ].iconLabel;
							if( msg.list[a].Title == 'Home' )
								img = '/iconthemes/friendup15/DriveLabels/Home.svg';
							else if( msg.list[a].Title == 'System' )
								img = '/iconthemes/friendup15/DriveLabels/SystemDrive.svg';
							
							var i = document.createElement( 'div' );
							i.className = 'FileBrowserItemImage';
							i.style.backgroundImage = 'url("' + img + '")';
							nm.appendChild( i );
							nm.classList.remove( 'IconSmall' );
							nm.classList.remove( 'IconDisk' );
						}
						
						d.appendChild( nm );
						
						if( msg.list[a].Type && msg.list[a].Type == 'bookmark' )
						{
							// Set nice folder icon
							nm.classList.remove( 'IconSmall' );
							nm.classList.remove( 'IconDisk' );
							var img = '/iconthemes/friendup15/DriveLabels/Bookmark.svg';
							var i = document.createElement( 'div' );
							i.className = 'FileBrowserItemImage';
							i.style.backgroundImage = 'url("' + img + '")';
							nm.appendChild( i );
							
							( function( ls ){
								var ex = document.createElement( 'span' );
								ex.className = 'FloatRight IconButton IconSmall fa-remove';
								ex.onclick = function( e )
								{
									var m = new Module( 'system' );
									m.onExecuted = function( e, d )
									{
										if( e == 'ok' )
										{
											self.clear();
											self.refresh();
										}
										else
										{
											console.log( 'Could not remove bookmark: ', e, d );
										}
									}
									m.execute( 'removebookmark', { name: ls.Path } );
									return cancelBubble( e );
								}
								nm.appendChild( ex );
							} )( msg.list[a] );
						}
						
						var s = document.createElement( 'div' );
						s.className = 'SubItems';
						d.appendChild( s );
						rootElement.appendChild( d );
						createOnclickAction( d, d.path, 'volume', depth + 1 );
					}
					// Existing items
					else
					{
						// Only refresh child elements if not open
						if( foundItem.classList.contains( 'Open' ) )
						{
							var s = foundItem.getElementsByClassName( 'SubItems' );
							if( s && s.length && msg.list[a].Volume )
							{
								self.refresh( msg.list[a].Volume, s[0], false, depth + 1 );
							}
						}
						
						if( !clickElement && self.flags.path && targetPath == foundItem.path )
						{
							clickElement = foundItem;
						}
					}
				}
				// Add checkers classes
				var sw = 2;
				for( var a = 0; a < eles.length; a++ )
				{
					sw = sw == 2 ? 1 : 2;
					if( eles[a].className && eles[a].classList.contains( 'DiskItem' ) )
					{
						eles[a].classList.remove( 'sw1' );
						eles[a].classList.remove( 'sw2' );
						eles[a].classList.add( 'sw' + sw );
					}
				}
				
				// Click the click element for path
				if( clickElement )
				{
					self.lastClickElement = clickElement; // store it
					setTimeout( function()
					{
						clickElement.onclick( { mode: 'open' } );
					}, 5 );
				}
			}
			
			if( self.flags.bookmarks )
			{
				var m = new Module( 'system' );
				m.onExecuted = function( e, d )
				{
					if( e == 'ok' )
					{
						try
						{
							var js = JSON.parse( d );
							msg.list.push( {
								Title: i18n( 'i18n_bookmarks' ) + ':',
								Type: 'header'
							} );
							for( var a = 0; a < js.length; a++ )
							{
								var ele = {
									Title: js[a].name,
									Type: 'bookmark',
									Path: js[a].path,
									Volume: js[a].path
								};
								msg.list.push( ele );
							}
						}
						catch( e )
						{
						}
						done();
					}
					else
					{
						done();
					}
				}
				m.execute( 'getbookmarks' );
			}
			else
			{
				done();
			}
		} );
	}
	// Get sub directories
	else
	{
		// Support both API scope and Workspace scope
		var func = function( path, flags, cb )
		{
			if( window.Workspace )
			{
				Friend.DOS.getDirectory( path, flags, function( response, msg )
				{
					cb( response ? {
						list: msg
					} : false );
				} );
			}
			else
			{
				Friend.DOS.getDirectory( path, flags, cb );
			}
		}
	
		func( path, { sort: true }, function( msg )
		{
			if( !msg || !msg.list ) return;
			
			// Create metadirectory "root"
			if( depth == 1 )
			{
				var itm = [ { Type: 'Directory', Filename: i18n( 'i18n_root_directory' ), Path: Application.browserPath, Title: i18n( 'i18n_root_directory' ), MetaType: 'RootDirectory' } ];
				msg.list = itm.concat( msg.list );
			}

			if( callback ) callback();
			
			// Get existing
			var eles = rootElement.childNodes;
						
			var removers = [];
			var found = [];
			var foundElements = [];
			for( var a = 0; a < eles.length; a++ )
			{
				var elFound = false;
				for( var b = 0; b < msg.list.length; b++ )
				{
					if( eles[a].id == 'fileitem_' + msg.list[b].Filename.split( ' ' ).join( '' ) )
					{
						var fn = msg.list[b].Filename;
						if( msg.list[b].Type == 'Directory' )
							fn += '/';
						
						// Special case - isn't really a directory (uses path without filename)
						if( msg.list[b].MetaType == 'RootDirectory' )
						{
							createOnclickAction( eles[a], path, 'RootDirectory', depth + 1 );
						}
						else
						{
							createOnclickAction( eles[a], path + fn, msg.list[b].Type, depth + 1 );
						}
						
						// Don't add twice
						if( !found.find( function( ele ){ ele == msg.list[b].Filename } ) )
						{
							found.push( msg.list[b].Filename );
							foundElements.push( eles[a] );
						}
						elFound = true;
					}
				}
				// Deleted item
				if( !elFound )
				{
					removers.push( eles[a] );
				}
			}
			for( var a = 0; a < removers.length; a++ )
			{
				rootElement.removeChild( removers[a] );
			}
			
			delete removers;
			
			// Precalc
			var d13 = depth * 13;
			for( var a = 0; a < msg.list.length; a++ )
			{
				var foundItem = false;
				if( msg.list[a].Filename.substr( 0, 1 ) == '.' ) continue; // skip hidden files
				for( var b = 0; b < found.length; b++ )
				{
					if( found[b] == msg.list[a].Filename )
					{
						foundItem = foundElements[b];
						break;
					}
				}
				if( !foundItem )
				{
					if( msg.list[a].Type == 'Directory' || ( self.callbacks && self.callbacks.permitFiletype && self.callbacks.permitFiletype( path + msg.list[a].Filename ) ) )
					{
						// Not displaying files?
						if( msg.list[a].Type != 'Directory' && !self.flags.displayFiles )
						{
							continue;
						}
						var d = document.createElement( 'div' );
						
						d.className = msg.list[a].Type == 'Directory' ? 'FolderItem' : 'FileItem';
						var ext = msg.list[a].Filename.split( '.' ).pop().toLowerCase();
						var icon = d.className == 'FolderItem' ? 'IconFolder' : ( 'IconFile ' + ext );
						d.id = 'fileitem_' + msg.list[a].Filename.split( ' ' ).join( '' );
						d.innerHTML = '<div style="padding-left: ' + ( d13 ) + 'px" class="Name IconSmall ' + icon + '"><span> ' + msg.list[a].Filename + '</span></div><div class="SubItems"></div>';
						rootElement.appendChild( d );
						var fn = msg.list[a].Filename;
						if( msg.list[a].Type == 'Directory' )
							fn += '/';
						d.path = path + fn;
						
						if( msg.list[a].MetaType == 'RootDirectory' )
						{
							d.path = self.rootPath;
							msg.list[a].Type = 'RootDirectory';
						}
						
						createOnclickAction( d, d.path, msg.list[a].Type, depth + 1 );
						
						// We have an incoming path
						if( !clickElement && self.flags.path && targetPath == d.path )
						{
							clickElement = d;
						}
					}
				}
				else if( foundItem && msg.list[a].Type == 'Directory' )
				{
					// Remove active state from inactive items
					var nam = foundItem.querySelector( '.Name' );
					if( nam && nam.classList.contains( 'Active' ) && foundItem.path != self.flags.path )
						nam.classList.remove( 'Active' );
					// Existing items
					if( foundItem.classList.contains( 'Open' ) )
					{
						if( msg.list[a].MetaType && msg.list[a].MetaType != 'RootDirectory' )
						{
							var s = foundItem.getElementsByClassName( 'SubItems' );
							if( s && s.length )
							{
								var fn = msg.list[a].Filename;
								if( msg.list[a].Type == 'Directory' )
									fn += '/';
								self.refresh( path + fn, s[0], false, depth + 1 );
							}
						}
					}
					// We have an incoming path
					if( !clickElement && self.flags.path && targetPath == foundItem.path )
					{
						clickElement = foundItem;
					}
				}
			}
			// Checkers
			var rootPathLength = self.rootPath.split( '/' ).length;
			var sw = 2;
			for( var a = 0; a < eles.length; a++ )
			{
				sw = sw == 2 ? 1 : 2;
				if( eles[a].className && ( eles[a].classList.contains( 'FolderItem' ) || eles[a].classList.contains( 'FileItem' ) ) )
				{
					eles[a].classList.remove( 'sw1' );
					eles[a].classList.remove( 'sw2' );
					eles[a].classList.add( 'sw' + sw );
				}
			}
			
			// Click the click element for path
			if( clickElement )
			{
				self.lastClickElement = clickElement; // Store it
				setTimeout( function()
				{
					clickElement.onclick();
				}, 50 );
			}
		} );
	}
};

