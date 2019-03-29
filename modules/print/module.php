<?php

/*©lgpl*************************************************************************
*                                                                              *
* This file is part of FRIEND UNIFYING PLATFORM.                               *
* Copyright (c) Friend Software Labs AS. All rights reserved.                  *
*                                                                              *
* Licensed under the Source EULA. Please refer to the copy of the GNU Lesser   *
* General Public License, found in the file license_lgpl.txt.                  *
*                                                                              *
*****************************************************************************©*/

require_once( 'php/friend.php' );
require_once( 'php/classes/dbio.php' );

// Get user level
if( $level = $SqlDatabase->FetchObject( '
	SELECT g.Name FROM FUserGroup g, FUserToGroup ug
	WHERE
		g.Type = \'Level\' AND
		ug.UserID=\'' . $User->ID . '\' AND
		ug.UserGroupID = g.ID
' ) )
{
	$level = $level->Name;
}
else $level = false;

// User level ------------------------------------------------------------------
if( $level != 'Admin' )
{
	switch( $args->command )
	{
		case 'list':
			// TODO: Apply user permissions
			if( isset( $args->args->id ) )
			{
				if( $row = $SqlDatabase->FetchObject( '
					SELECT `ID`, `Data` 
					FROM FSetting 
					WHERE `UserID` = "0" AND `Type` = "system" AND `Key` = "printer" AND ID = "' . $args->args->id . '"
					ORDER BY `ID` ASC 
				' ) )
				{
					die( 'ok<!--separate-->' . json_encode( $row ) );
				}
				else
				{
					die( 'fail<!--separate-->{"response":-1,"message":"No printer found"}' );
				}
			}
			// TODO: Apply user permissions
			else
			{
				if( $rows = $SqlDatabase->FetchObjects( '
					SELECT `ID`, `Data` 
					FROM FSetting 
					WHERE `UserID` = "0" AND `Type` = "system" AND `Key` = "printer" 
					ORDER BY `ID` ASC 
				' ) )
				{
					die( 'ok<!--separate-->' . json_encode( $rows ) );
				}
				else
				{
					die( 'fail<!--separate-->{"response":-1,"message":"No printers found"}' );
				}
			}
			break;
		case 'print':
			
			break;
	}
}
// Admin level -----------------------------------------------------------------
else
{

	switch( $args->command )
	{
	
		// read ----------------------------------------------------------------- //
	
		case 'list': 
		
			if( isset( $args->args->id ) )
			{
				if( $row = $SqlDatabase->FetchObject( '
					SELECT `ID`, `Data` 
					FROM FSetting 
					WHERE `UserID` = "0" AND `Type` = "system" AND `Key` = "printer" AND ID = "' . $args->args->id . '"
					ORDER BY `ID` ASC 
				' ) )
				{
					die( 'ok<!--separate-->' . json_encode( $row ) );
				}
				else
				{
					die( 'fail<!--separate-->{"response":-1,"message":"No printer found"}' );
				}
			}
			else
			{
				if( $rows = $SqlDatabase->FetchObjects( '
					SELECT `ID`, `Data` 
					FROM FSetting 
					WHERE `UserID` = "0" AND `Type` = "system" AND `Key` = "printer" 
					ORDER BY `ID` ASC 
				' ) )
				{
					die( 'ok<!--separate-->' . json_encode( $rows ) );
				}
				else
				{
					die( 'fail<!--separate-->{"response":-1,"message":"No printers found"}' );
				}
			}
		
			break;
	
		// write ---------------------------------------------------------------- //
	
		case 'create':
		
			if( $args->args->data )
			{
				$o = new dbIO( 'FSetting' );
				$o->UserID = 0;
				$o->Type = 'system';
				$o->Key = 'printer';
				$o->Data = json_encode( $args->args->data );
				$o->Save();
			
				die( 'ok<!--separate-->' . $o->ID );
			}
		
			die( 'fail<!--separate-->{"response":-1,"message":"Could not create printer"}' );
		
			break;
	
		case 'update':
		
			if( $args->args->id && $args->args->data )
			{
				$o = new dbIO( 'FSetting' );
				$o->ID = $args->args->id;
				$o->UserID = 0;
				$o->Type = 'system';
				$o->Key = 'printer';
				if( $o->Load() )
				{
					$id = $o->ID;
					$o->Data = json_encode( $args->args->data );
					$o->Save();
				
					die( 'ok<!--separate-->' . $o->ID );
				}
			}
		
			die( 'fail<!--separate-->{"response":-1,"message":"Could not update printer"}' );
		
			break;
		
		// delete --------------------------------------------------------------- //
	
		case 'remove':
		
			if( $args->args->id )
			{
				$o = new dbIO( 'FSetting' );
				$o->ID = $args->args->id;
				$o->UserID = 0;
				$o->Type = 'system';
				$o->Key = 'printer';
				if( $o->Load() )
				{
					$id = $o->ID;
					$o->Delete();
				
					die( 'ok<!--separate-->' . $id );
				}
			}
		
			die( 'fail<!--separate-->{"response":-1,"message":"Could not remove printer"}' );
		
			break;
	
	}
}

//die( print_r( $args,1 ) . ' --' );

die( 'fail' );

?>