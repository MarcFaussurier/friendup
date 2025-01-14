/*©mit**************************************************************************
*                                                                              *
* This file is part of FRIEND UNIFYING PLATFORM.                               *
* Copyright (c) Friend Software Labs AS. All rights reserved.                  *
*                                                                              *
* Licensed under the Source EULA. Please refer to the copy of the MIT License, *
* found in the file license_mit.txt.                                           *
*                                                                              *
*****************************************************************************©*/
/** @file
 * 
 *  Protocol HTTP parser
 *
 *  @author HT
 *  @date pushed 14/10/2015
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "core/friend_core.h"
#include "core/library.h"

#include "util/string.h"
#include "util/murmurhash3.h"

#include "network/uri.h"
#include "network/http.h"
#include "network/mime.h"
#include "network/websocket.h"
#include "network/protocol_http.h"

#include <system/systembase.h>
#include <network/locfile.h>
#include <system/systembase.h>
#include <system/cache/cache_manager.h>
#include <util/tagitem.h>
#include <util/list_string.h>
#include <network/protocol_webdav.h>
#include <system/fsys/file.h>
#include <system/fsys/device_handling.h>

#include <system/user/user_sessionmanager.h>
#include <system/user/user_manager.h>
#include <system/web_util.h>

#include <util/newpopen.h>

#define HTTP_REQUEST_TIMEOUT 2 * 60
#define SHARING_BUFFER_SIZE 262144

extern SystemBase *SLIB;

// external

char *GetArgsAndReplaceSession( Http *request, UserSession *loggedSession, FBOOL *arg );

// 
//	TODO: This should be moved
//It is to help us with fallback PHP support
//
/**
 * Function runs php script via PHP
 *
 * @param command pointer to php command provided as string
 * @return new ListString structure or NULL when problem appear
 */
static inline ListString *RunPHPScript( const char *command )
{
	NPOpenFD pofd;
	int err = newpopen( command, &pofd );
	if( err != 0 )
	{
		FERROR("[RunPHPScript] cannot open pipe: %s\n", strerror( errno ) );
		return NULL;
	}
	
#define PHP_READ_SIZE 65536	
	
	char *buf = FMalloc( PHP_READ_SIZE+16 );
	ListString *data = ListStringNew();
	int errCounter = 0;
	int size = 0;
	
	fd_set set;
	struct timeval timeout;

	// Initialize the timeout data structure. 
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	
	while( TRUE )
	{
			/* Initialize the file descriptor set. */
		FD_ZERO( &set );
		FD_SET( pofd.np_FD[ NPOPEN_CONSOLE ], &set);
		DEBUG("[RunPHPScript] in loop\n");
		
		int ret = select( pofd.np_FD[ NPOPEN_CONSOLE ]+1, &set, NULL, NULL, &timeout );
		// Make a new buffer and read
		if( ret == 0 )
		{
			DEBUG("Timeout!\n");
			break;
		}
		else if(  ret < 0 )
		{
			DEBUG("Error\n");
			break;
		}
		size = read( pofd.np_FD[ NPOPEN_CONSOLE ], buf, PHP_READ_SIZE);

		DEBUG( "[RunPHPScript] Adding %d of data\n", size );
		if( size > 0 )
		{
			DEBUG( "[RunPHPScript] before adding to list\n");
			ListStringAdd( data, buf, size );
			DEBUG( "[RunPHPScript] after adding to list\n");
		}
		else
		{
			errCounter++;
			DEBUG("ErrCounter: %d\n", errCounter );
			if( errCounter > 3 )
			{
				//char clo[2];
				//clo[0] = '\'';
				//clo[1] = EOF;
				//write( pofd.np_FD[ NPOPEN_INPUT ], clo, 2 );
				//FERROR("Error in popen, Quit! Command: %s\n", command );
				break;
			}
		}
	}
	DEBUG( "[RunPHPScript] after loop, memory will be released\n");
	
	FFree( buf );
	DEBUG("[RunPHPScript] File readed\n");
	
	// Free pipe if it's there
	newpclose( &pofd );
	
	ListStringJoin( data );		//we join all string into one buffer

	DEBUG( "[RunPHPScript] Finished PHP call...(%lu length)-\n", data->ls_Size );
	return data;
	
	/*
	FILE *pipe = popen( command, "r" );
	if( !pipe )
	{
		Log( FLOG_ERROR,"Cannot open pipe for command: %s\n", command );
		return NULL;
	}

	FULONG size = 0;
	ListString *data = ListStringNew();

#define PHP_READ_SIZE 65536
	char *buf = FCalloc( PHP_READ_SIZE, sizeof( char ) );
	if( buf != NULL )
	{
		while( !feof( pipe ) )
		{
			// Make a new buffer and read
			size = fread( buf, sizeof(char), PHP_READ_SIZE, pipe );

			if( size > 0 )
			{
				ListStringAdd( data, buf, size );
			}
			pthread_yield();
		}
		ListStringJoin( data );		
		FFree( buf );
	}
	// Free buffer if it's there
	pclose( pipe );

	return data;
	*/
}

/**
 * Read single local file
 * 
 * we assume that mime is not neccessary for many files in one
 *
 * @param uri pointer to Uri structure
 * @param locpath pointer to file path
 * @param dstbs pointer to BufString when data will be stored
 * @param result pointer to place where http status will be stored
 * @return 0 when success, otherwise NULL
 */
static inline int ReadServerFile( Uri *uri __attribute__((unused)), char *locpath, BufString *dstbs, int *result )
{
	Path *base = PathNew( "resources" );
	if( base == NULL )
	{
		Log( FLOG_ERROR,"Cannot create base path!\n");
		return -1;
	}

	DEBUG("[ReadServerFile] path %s\n", locpath );
	Path *convPath = PathNew( locpath );
	if( convPath == NULL )
	{
		PathFree( base );
		Log( FLOG_ERROR,"Cannot read file from server\n");
		return -2;
	}

	PathResolve( convPath ); 

	Path* completePath = PathJoin( base, convPath );
	if( completePath == NULL )
	{
		Log( FLOG_ERROR,"Cannot create completePath!\n");
		PathFree( base );
		PathFree( convPath );
		return -3;
	}

	FBOOL freeFile = FALSE;

	LocFile* file = NULL;
	//if( FRIEND_MUTEX_LOCK( &SLIB->sl_ResourceMutex ) == 0 )
	{
		if( SLIB->sl_CacheFiles == 1 )
		{
			file = CacheManagerFileGet( SLIB->cm, completePath->raw, FALSE );

			if( file == NULL )
			{
				char *decoded = UrlDecodeToMem( completePath->raw );
				file = LocFileNew( decoded, FILE_READ_NOW | FILE_CACHEABLE );
				FFree( decoded );

				if( file != NULL )
				{
					if( CacheManagerFilePut( SLIB->cm, file ) != 0 )
					{
						freeFile = TRUE;
					}
				}
				else
				{
					Log( FLOG_ERROR,"Cannot read file %s\n", completePath->raw );
				}
			}
			else
			{
				struct stat attr;
				stat( completePath->raw, &attr);

				// if file is new file, reload it
				//DEBUG1("\n\n\n\n\n SIZE %lld  stat %lld\n\n\n\n",attr.st_mtime ,file->info.st_mtime );
				if( attr.st_mtime != file->lf_Info.st_mtime )
				{
					LocFileReload( file, completePath->raw);
				}
			}
		}
		else
		{
			char *decoded = UrlDecodeToMem( completePath->raw );
			file = LocFileNew( decoded, FILE_READ_NOW | FILE_CACHEABLE );
			FFree( decoded );
			if( file == NULL )
			{
				Log( FLOG_ERROR,"Cannot load file in developer mode %s\n", completePath->raw );
			}
			else
			{
				freeFile = TRUE;
			}
		}
		//FRIEND_MUTEX_UNLOCK( &SLIB->sl_ResourceMutex );
	}

	// Send reply
	if( file != NULL )
	{
		if(  file->lf_Buffer == NULL )
		{
			Log( FLOG_ERROR,"File is empty %s\n", completePath->raw );
		}

		BufStringAddSize( dstbs, file->lf_Buffer, file->lf_FileSize );
		BufStringAdd( dstbs, "\n");

		if( freeFile == TRUE )
		{
			LocFileDelete( file );
		}

		*result = 200;
	}
	else
	{
		// Try to fall back on module
		// TODO: Make this behaviour configurable
		char *command;
		
		DEBUG("CatchALL 230\n");

		int loclen = strlen( locpath ) + 256;
		if( ( command = FCalloc( loclen, sizeof( char ) ) ) != NULL )
		{
			snprintf( command, loclen, "php \"php/catch_all.php\" \"%s\";", locpath ); 
			DEBUG( "[ReadServerFile] Executing %s\n", command );
			int phpRun = FALSE;
			ListString *bs = RunPHPScript( command );
			if( bs )
			{
				if( bs->ls_Size > 0 )
				{
					BufStringAddSize( dstbs, bs->ls_Data, bs->ls_Size );
					BufStringAdd( dstbs, "\n");
				}

				phpRun = TRUE;
				*result = 200;

				ListStringDelete( bs );
			}

			if( !phpRun )
			{
				Log( FLOG_INFO, "[ReadServerFile] File do not exist %s\n", locpath );

				*result = 404;
			}

			FFree( command );
		}
	}
	PathFree( base );
	PathFree( completePath );
	PathFree( convPath );

	return 0;
}

/**
 * Http protocol parser
 *
 * @param sock pointer to Socket from which request will readed and which will be used to return response
 * @param data pointer to already received data
 * @param length length of already received data
 * @return new Http structrure when success, otherwise NULL
 */
Http *ProtocolHttp( Socket* sock, char* data, unsigned int length )
{
	Http *response = NULL;
	Log( FLOG_DEBUG,"[ProtocolHttp] HTTP Callback called\n");

	if( length <= 0 )
	{
		Log( FLOG_DEBUG,"[ProtocolHttp] Message length<0 http400\n");

		struct TagItem tags[] = {
				{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicateN( "close", 5 ) },
				{ TAG_DONE, TAG_DONE }
		};

		response = HttpNewSimple( HTTP_400_BAD_REQUEST, tags );

		return response;
	}

	// Get the current request we're working on, or start a new one
	Http* request = (Http*)sock->data;
	if( !request )
	{
		request = HttpNew( );
		request->timestamp = time( NULL );
		sock->data = (void*)request;
		request->h_Socket = sock;
	}

	//DEBUG("time %ld\nreqtimestamp %ld\nreqtimestamp %ld\n",
	//	  time( NULL ), request->timestamp, HTTP_REQUEST_TIMEOUT );
	// Timeout
	/*
	if( time( NULL ) > request->timestamp + HTTP_REQUEST_TIMEOUT )
	{
		struct TagItem tags[] = {
			{ HTTP_HEADER_CONTENT_TYPE, (FULONG)  StringDuplicate( "text/plain" ) },
			{	HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
			{TAG_DONE, TAG_DONE}
		};

		response = HttpNewSimple( HTTP_408_REQUEST_TIME_OUT,  tags );

		HttpAddTextContent( response, "408 Request Timeout\n" );
		//HttpWriteAndFree( response );
		HttpFreeRequest( request );
		sock->data = NULL;
		DEBUG("HTTP TIMER\n");
		return response;
	}*/

	DEBUG("[ProtocolHttp] Data delivered %d\n", length );
	// Continue parsing the request
	int result = HttpParsePartialRequest( request, data, length );

	partialRequest:

	// Protocol error
	if( result < 0 )
	{
		Log( FLOG_DEBUG, "[ProtocolHttp] RESULT < 0 http 400 will be returned\n");
		struct TagItem tags[] = {
				{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
				{ TAG_DONE, TAG_DONE }
		};

		response = HttpNewSimple( HTTP_400_BAD_REQUEST, tags );

		//HttpWriteAndFree( response );
	}
	// Request not fully parsed yet. Return and wait for more data
	else if( result == -1 )
	{
		DEBUG( "[ProtocolHttp] <- (%d): Waiting for more data\n", sock->fd );
		HttpFreeRequest( request );
		return response;
	}
	else if (result == 1 && request->uri == NULL)
	{
		struct TagItem tags[] = {
				{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
				{ TAG_DONE, TAG_DONE }
		};

		response = HttpNewSimple( HTTP_400_BAD_REQUEST, tags );
	}
	else if( result == 1 && request->uri->redirect == TRUE && request->uri->queryRaw )
	{
		Log( FLOG_DEBUG, "[ProtocolHttp] Redirect\n" );
		struct TagItem tags[] = {
				{ HTTP_HEADER_LOCATION, (FULONG)StringDuplicateN( request->uri->queryRaw, strlen( request->uri->queryRaw ) ) },
				{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
				{ TAG_DONE, TAG_DONE }
		};

		response = HttpNewSimple( HTTP_307_TEMPORARY_REDIRECT, tags );

		result = 307;
	}
	// Request parsed without errors!
	else if( result == 1 && request->uri->path != NULL )
	{
		Log( FLOG_DEBUG, "[ProtocolHttp] Request parsed without problems.\n");
		Uri *uri = request->uri;
		Path *path = NULL;
		if( uri->path->raw )
		{
			int nlen = 0;
			for( ; ; nlen++ )
			{
				if( !uri->path->raw[nlen] )
				{
					break;
				}
			}
			DEBUG("[ProtocolHttp] Want to parse path: %s (%d)\n", uri->path->raw, nlen );
			path = PathNew( uri->path->raw );
			if( path )
			{
				PathResolve( path );  // Resolve checks for "../"'s, and removes as many as it can.
			}
		}

		// Disallow proxy requests
		if( uri && ( uri->scheme || uri->authority ) )
		{
			DEBUG("[ProtocolHttp] Dissalow proxy\n");
			struct TagItem tags[] = {
					{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
					{ TAG_DONE, TAG_DONE }
			};

			response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );

			result = 403;
		}

		//
		// WEBDAV
		//

		else if( strcmp( "webdav", path->parts[ 0 ] ) == 0 ) //if( (request->h_ContentType == HTTP_CONTENT_TYPE_APPLICATION_XML || request->h_ContentType == HTTP_CONTENT_TYPE_TEXT_XML ) &&
		{
			response = HandleWebDav( SLIB, request, request->content, request->sizeOfContent );

			result = 200;
		}

		//
		// Cross-domain requests uses a pre-flight OPTIONS call
		//

		else if( !request->errorCode && request->method && strcmp( request->method, "OPTIONS" ) == 0 )
		{
			struct TagItem tags[] = {
					{ HTTP_HEADER_CONTROL_ALLOW_ORIGIN, (FULONG)StringDuplicateN( "*", 1 ) },
					{ HTTP_HEADER_CONTROL_ALLOW_HEADERS, (FULONG)StringDuplicateN( "Origin, X-Requested-With, Content-Type, Accept, Method", 54 ) },
					{ HTTP_HEADER_CONTROL_ALLOW_METHODS,  (FULONG)StringDuplicateN( "GET, POST, OPTIONS", 18 ) },
					{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicateN( "close", 5 ) },
					{ TAG_DONE, TAG_DONE }
			};

			response = HttpNewSimple( HTTP_200_OK,  tags );

			result = 200;
		}		
		// Check for connection upgrade
		else if( !request->errorCode && HttpHeaderContains( request, "connection", "Upgrade", false ) )
		{
			struct TagItem tags[] = {
					{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
					{ TAG_DONE, TAG_DONE }
			};

			response = HttpNewSimple(  HTTP_400_BAD_REQUEST, tags );

		}
		else
		{
			if( !path || !path->resolved ) // If it cannot remove all, path->resolved == false.
			{
				Log( FLOG_ERROR, "404 error, path is equal to NULL\n");

				struct TagItem tags[] = {
						{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
						{ TAG_DONE, TAG_DONE }
				};

				response = HttpNewSimple( HTTP_403_FORBIDDEN,  tags );

				result = 403;
			}
			else
			{
				{
					if( path->size >= 2 && StringCheckExtension( path->parts[0], "library" ) == 0 )
					{
						// system.library is main library and should be use for most things
						// we open it and close in main

						if( strcmp( path->parts[ 0 ], "system.library" ) == 0 )
						{
							DEBUG("[ProtocolHttp] -----------------------------------------------------Calling SYSBASE via HTTP %s\n", path->parts[1] );

							int respcode = 0;
							response = SLIB->SysWebRequest( SLIB, &(path->parts[1]), &request, NULL, &respcode );

							if( response == NULL )
							{
								struct TagItem tags[] = {
										{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
										{ TAG_DONE, TAG_DONE }
								};	

								response = HttpNewSimple(  HTTP_500_INTERNAL_SERVER_ERROR,  tags );

								result = 500;
							}
							else
							{
								Log( FLOG_INFO, "[HTTP] SysWebRequest response: '%.*s'\n", 200, response->content );
							}
						}
						else
						{
							UserSession *session = NULL;
							HashmapElement *tst = GetHEReq( request, "sessionid" );
							if( tst != NULL )
							{
								if( tst->data != NULL )
								{
									session = SLIB->sl_USM->usm_Sessions;
									while( session != NULL )
									{
										if(  strcmp( session->us_SessionID, (char *)tst->data ) == 0 )
										{
											break;
										}
										session = (UserSession *)session->node.mln_Succ;
									}
								}
							}
							UserLoggerStore( SLIB->sl_ULM, session, request->rawRequestPath, request->h_UserActionInfo );

							FriendCoreInstance_t *fci = (FriendCoreInstance_t *) sock->s_Data;
							Library* lib = FriendCoreGetLibrary( fci, path->parts[0], 1 );
							if( lib && lib->WebRequest )
							{
								response = lib->WebRequest( lib, path->parts[1], request );
								if( response == NULL )
								{
									struct TagItem tags[] = {
											{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
											{ TAG_DONE, TAG_DONE }
									};	

									response = HttpNewSimple( HTTP_500_INTERNAL_SERVER_ERROR,  tags );

									result = 500;
								}
							}
							else
							{
								struct TagItem tags[] = {
										{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
										{ TAG_DONE, TAG_DONE }
								};	

								response = HttpNewSimple( HTTP_404_NOT_FOUND,  tags );

								result = 404;
							}
						}
					}
	/*				
					else if( strcmp( path->parts[ 0 ], "loginprompt" ) == 0 )
					{
						
					}
*/
	
					else if( strcmp( path->parts[ 0 ], "version" ) == 0 )
					{
						struct TagItem tags[] = {
							{ HTTP_HEADER_CONTENT_TYPE, (FULONG) StringDuplicate("text/html") },
							{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
							{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "max-age = 3600" ) },
							{ TAG_DONE, TAG_DONE}
						};

						response = HttpNewSimple( HTTP_200_OK, tags );
						
						{
							char buf[ 128 ];
							snprintf( buf, 128, "%s-%s", APPVERSION, APPGITVERSION );
							HttpAddTextContent( response, buf );
						}

						HttpWrite( response, sock );
						result = 200;
					}
						
	
					//
					// login path file
					//

					else if( strcmp( path->parts[ 0 ], "loginprompt" ) == 0 )
					{
						
						//
						// Check if redirect is required
						//
						
						char *newUrl = NULL;
						

						{
							if( strcmp( SLIB->sl_ActiveModuleName, "fcdb.authmod" ) != 0 )
							{
								FULONG res = 0;

#define MAX_LEN_PHP_INT_COMMAND 1024
								char *command = FMalloc( MAX_LEN_PHP_INT_COMMAND );

								// Make the commandline string with the safe, escaped arguments, and check for buffer overflows.
								int cx = snprintf( command, MAX_LEN_PHP_INT_COMMAND-1, "php \"php/login.php\" \"%s\" \"%s\" \"%s\"; 2>&1", uri->path->raw, uri->queryRaw, request->content ); // SLIB->sl_ModuleNames
								//if( !( cx >= 0 ) )
								//{
								//	FERROR( "[ProtocolHttp] snprintf\n" );;
								//}
								//else
								{
									ListString *ls = RunPHPScript( command );
									if( ls != NULL )
									{
										//DEBUG("\n\n\n\n\n\nDATA: %s\n\n\n\n\n\n", ls->ls_Data );
										res = ls->ls_Size;
									}
									/*
									FILE *pipe = popen( command, "r" );
									ListString *ls = NULL;
									
									Log( FLOG_INFO, "Sending php command: %s < pipe: %p\n", command, pipe );

									if( pipe != NULL )
									{
										ls = ListStringNew();
										char buffer[ 1024 ];

										while( !feof( pipe ) )
										{
											int reads = fread( buffer, sizeof( char ), 1024, pipe );
											if( reads > 0 )
											{
												ListStringAdd( ls, buffer, reads );
												res += reads;
											}
										}
										pclose( pipe );
									}
									else
									{
										Log( FLOG_ERROR, "Cannot open pipe!\n");
									}
									
									Log( FLOG_INFO, "End of PHP loop\n");

									if( ls != NULL )
									{
										ListStringJoin( ls );
									}
									*/
									
									struct TagItem tags[] = {
										{ HTTP_HEADER_CONTENT_TYPE, (FULONG) StringDuplicate("text/html") },
										{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
										{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "max-age = 3600" ) },
										{TAG_DONE, TAG_DONE}
									};

									response = HttpNewSimple( HTTP_200_OK, tags );

									if( ls != NULL && ls->ls_Data != NULL )
									{
										HttpSetContent( response, ls->ls_Data, res );
									}
									else
									{
										HttpAddTextContent( response, "fail<!--separate-->PHP script return error" );
									}

									// write here and set data to NULL!!!!!
									// return response
									HttpWrite( response, sock );
									result = 200;

									if( ls != NULL )
									{
										ls->ls_Data = NULL;
										ListStringDelete( ls );
									}
									DEBUG("Response delivered\n");
									
									FFree( command );
								}
							}

							//
							// default login page
							//

							else
							{
								FBOOL freeFile = FALSE;
								//Path *base = PathNew( "resources" );
								//Path *base = PathNew( "resources/webclient/templates/login_prompt.html" );
								//Path *base = PathNew( "/webclient/templates/login_prompt.html" );
								//if( base != NULL )
								{
									//Path* completePath = PathJoin( base, "templates/login_prompt.html" );
									//if( completePath != NULL )
									{
										LocFile *file = LocFileNew( "resources/webclient/templates/login_prompt.html", FILE_READ_NOW | FILE_CACHEABLE );
										if( file != NULL )
										{
											freeFile = TRUE;

											struct TagItem tags[] = {
												{ HTTP_HEADER_CONTENT_TYPE, (FULONG) StringDuplicate("text/html") },
												{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
												{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "max-age = 3600" ) },
												{ TAG_DONE, TAG_DONE }
											};

											response = HttpNewSimple( HTTP_200_OK, tags );

											HttpSetContent( response, file->lf_Buffer, file->lf_FileSize );

											// write here and set data to NULL!!!!!
											// retusn response
											HttpWrite( response, sock );
											result = 200;

											//INFO("--------------------------------------------------------------%d\n", freeFile );
											if( freeFile == TRUE )
											{
												LocFileDelete( file );
											}
											response->content = NULL;
											response->sizeOfContent = 0;

											response->h_WriteType = FREE_ONLY;
										}
									}
								}
							}
						}	// newUrl == NULL
					}

					//
					// share file
					//

					else if( strcmp( path->parts[ 0 ], "sharedfile" ) == 0 )
					{
						FileShared *fs = NULL;
						char query[ 1024 ];
						int entries = 0;

						Log( FLOG_DEBUG, "[ProtocolHttp] Shared file hash %s name %s\n", path->parts[ 1 ], path->parts[ 2 ] );

						SQLLibrary *sqllib = SLIB->LibrarySQLGet( SLIB );

						if( sqllib != NULL )
						{
							sqllib->SNPrintF( sqllib, query, sizeof(query), " `Hash` = '%s'", path->parts[ 1 ] );

							if( ( fs = sqllib->Load( sqllib, FileSharedTDesc, query, &entries ) ) != NULL )
							{
								char *error = NULL;
								// Immediately drop here..
								SLIB->LibrarySQLDrop( SLIB, sqllib );

								CacheFile *cf = NULL;

								char *mime = NULL;

								File *rootDev = GetUserDeviceByUserID( SLIB->sl_DeviceManager, sqllib, fs->fs_IDUser, fs->fs_DeviceName, &error );
								
								if( error != NULL )
								{
									FFree( error );
								}

								DEBUG("[ProtocolHttp] Device taken from DB/Session , devicename %s\n", fs->fs_DeviceName );

								if( rootDev != NULL )
								{
									FHandler *actFS = (FHandler *)rootDev->f_FSys;
									int cacheState = CACHE_NOT_SUPPORTED;

									char *extension = GetExtension( fs->fs_Path );

									// Use the extension if possible
									if( strlen( extension ) )
									{
										mime = StringDuplicate( MimeFromExtension( extension ) );
									}
									else
									{
										mime = StringDuplicate( "application/octet-stream" );
									}

									//add mounting and reading files from FS
									struct TagItem tags[] = {
											{ HTTP_HEADER_CONTENT_TYPE, (FULONG)mime },
											{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
											{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "max-age = 3600" ) },
											{ TAG_DONE, TAG_DONE }
									};

									// 0 = filesystem do not provide modify timestamp
									time_t tim = actFS->GetChangeTimestamp( rootDev, fs->fs_Path );
									// there is no need to cache files which are stored on local disk
									if( tim == 0 ) //|| strcmp( actFS->GetPrefix(), "local" ) )
									{

									}
									else
									{
										cf = CacheUFManagerFileGet( SLIB->sl_CacheUFM, fs->fs_IDUser, rootDev->f_ID, fs->fs_Path );

										// if TRUE file must be reloaded
										if( cf != NULL )
										{
											cf->cf_ModificationTimestamp = tim;
											if( cf->cf_ModificationTimestamp != tim )
											{
												cacheState = CACHE_FILE_REQUIRE_REFRESH;		// we can use same pointer to file, but there is need to store it again
											}
											else
											{
												cacheState = CACHE_FILE_CAN_BE_USED;		// cache have last file, we can use it
											}
										}
										else
										{
											cacheState = CACHE_FILE_MUST_BE_CREATED;		// file do not exist in cache, we can create new one
										}
									}

									if( cacheState == CACHE_FILE_CAN_BE_USED )
									{
										int resp = 0;
										int dataread = 0;

										cf->cf_Fp = fopen( cf->cf_StorePath, "rb" );
										if( cf->cf_Fp != NULL )
										{
											char *tbuffer = FMalloc( SHARING_BUFFER_SIZE );
											if( tbuffer != NULL )
											{
												while( !feof( cf->cf_Fp ) )
												{
													if( resp == 0 && dataread > 0 )
													{
														response = HttpNewSimple( HTTP_200_OK, tags );
														HttpWrite( response, request->h_Socket );
														resp = 1;
													}
													dataread = fread( tbuffer, 1, SHARING_BUFFER_SIZE, cf->cf_Fp );
													SocketWrite( request->h_Socket, tbuffer, dataread );
												}
												FFree( tbuffer );
											}
											fclose( cf->cf_Fp );
										}

										if( resp == 0 )
										{
											response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );
											HttpWrite( response, request->h_Socket );
										}

										result = 200;

										HttpFree( response );
										response = NULL;
									}
									else
									{
										DEBUG("CACHE STATE: %d\n", cacheState );
										FILE *cffp = NULL;

										if( cacheState == CACHE_FILE_MUST_BE_CREATED )
										{
											cf = CacheFileNew( fs->fs_Path );
											cf->cf_Fp = fopen( cf->cf_StorePath, "wb" );
											cf->cf_ModificationTimestamp = tim;
											cf->cf_FileSize = 0;
											cffp = cf->cf_Fp;
										}
										else if( cacheState == CACHE_FILE_REQUIRE_REFRESH )
										{
											cf->cf_Fp = fopen( cf->cf_StorePath, "wb" );
											cf->cf_FileSize = 0;
											cffp = cf->cf_Fp;
										}

										// We need to get the sessionId if we can!
										// currently from table we read UserID
										User *tuser = UMGetUserByID( SLIB->sl_UM, fs->fs_IDUser );

										if( tuser != NULL )
										{
											char *sess = USMUserGetFirstActiveSessionID( SLIB->sl_USM, tuser );
											/*
											if( sess && rootDev->f_SessionID )
											{
												FFree( rootDev->f_SessionID );
												rootDev->f_SessionID = StringDuplicate( tuser->u_MainSessionID );
												DEBUG("[ProtocolHttp] Session %s tusr ptr %p\n", sess, tuser );
											}
											*/
											if( sess )
											{
												rootDev->f_SessionIDPTR = tuser->u_MainSessionID;
												DEBUG("[ProtocolHttp] Session %s tusr ptr %p\n", sess, tuser );
											}
										}


										if( actFS != NULL )
										{
											char *filePath = fs->fs_Path;
											unsigned int i;

											for( i = 0; i < strlen( fs->fs_Path ); i++ )
											{
												if( fs->fs_Path[ i ] == ':' )
												{
													filePath = &(fs->fs_Path[ i + 1 ]);
												}
											}

											DEBUG("[ProtocolHttp] File will be opened now %s\n", filePath );

											File *fp = ( File *)actFS->FileOpen( rootDev, filePath, "rs" );
											if( fp != NULL )
											{
												int resp = 0;
												
												//fp->f_Stream = request->h_Stream;
												//fp->f_Socket = request->h_Socket;
												//fp->f_WSocket = request->h_WSocket;
												//fp->f_Stream = TRUE;

												int dataread;

												char *tbuffer = FMalloc( SHARING_BUFFER_SIZE );
												if( tbuffer != NULL )
												{
													DEBUG("tbuffer\n");
													while( ( dataread = actFS->FileRead( fp, tbuffer, SHARING_BUFFER_SIZE ) ) != -1 )
													{
														DEBUG("inside of loop: readed %d\n", dataread );
														if( resp == 0 && dataread > 0 )
														{
															response = HttpNewSimple( HTTP_200_OK, tags );
															HttpWrite( response, request->h_Socket );
															resp = 1;
															
															SocketWrite( request->h_Socket, tbuffer, dataread );
														}
														else
														{
															SocketWrite( request->h_Socket, tbuffer, dataread );
														}
														
														if( cffp != NULL )
														{
															DEBUG("Store %d\n", dataread );
															fwrite( tbuffer, 1, dataread, cffp );
															cf->cf_FileSize += dataread;
														}
													}
													FFree( tbuffer );
												}
												DEBUG("should I send fail? %d\n", resp );
												
												if( resp == 0 )
												{
													response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );
													HttpWrite( response, request->h_Socket );
												}

												result = 200;

												HttpFree( response );
												response = NULL;

												actFS->FileClose( rootDev, fp );
											}
											else
											{
												response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );
												
												result = 404;
												Log( FLOG_ERROR,"Cannot open file %s!\n", filePath );
											}
										}
										else
										{
											response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );
											
											result = 404;
											Log( FLOG_ERROR,"Cannot find filesystem for device!\n");
										}

										if( cacheState == CACHE_FILE_MUST_BE_CREATED )
										{
											fclose( cf->cf_Fp );
											CacheUFManagerFilePut( SLIB->sl_CacheUFM, fs->fs_IDUser, rootDev->f_ID, cf );
										}
										else if( cacheState == CACHE_FILE_REQUIRE_REFRESH )
										{
											fclose( cf->cf_Fp );
										}

									} // cache support
									FFree( extension );
								}
								else
								{
									struct TagItem tags[] = {
										{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
										{ TAG_DONE, TAG_DONE }
									};
									response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );
									
									result = 404;
									Log( FLOG_ERROR,"Cannot get root device\n");
								}
								FileSharedDelete( fs );
							}
							else
							{
								struct TagItem tags[] = {
									{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
									{ TAG_DONE, TAG_DONE }
								};
								response = HttpNewSimple( HTTP_403_FORBIDDEN, tags );
								
								SLIB->LibrarySQLDrop( SLIB, sqllib );
								result = 404;
								Log( FLOG_ERROR,"Fileshared entry not found in DB: sql %s\n", query );
							}
						}
					}

					//
					// We're calling on a static file.
					//

					else
					{
						UserSession *session = NULL;
						HashmapElement *tst = GetHEReq( request, "sessionid" );
						if( tst != NULL )
						{
							if( tst->data != NULL )
							{
								session = SLIB->sl_USM->usm_Sessions;
								while( session != NULL )
								{
									if(  strcmp( session->us_SessionID, (char *)tst->data ) == 0 )
									{
										break;
									}
									session = (UserSession *)session->node.mln_Succ;
								}
							}
						}
						UserLoggerStore( SLIB->sl_ULM, session, request->rawRequestPath, request->h_UserActionInfo );

						// Read the file

						unsigned int i = 0;
						int pos = -1;
						int flaw = 0;

						// Make sure we're not having an exploit here
						for( ; i < path->rawSize - 1; i++ )
						{
							if( path->raw[ i ] == '.' && path->raw[ i + 1 ] == '.' )
							{
								flaw = 1;
								break;
							}
						}
						// We don't allow directory traversals..
						if( flaw == 0 )
						{
							Log( FLOG_DEBUG, "[ProtocolHttp] read static file %s size %d\n", path->raw, path->rawSize );

							for( i = 0; i < path->rawSize; i++ )
							{
								if( path->raw[ i ] == ';' )
								{
									pos = i;
									break;
								}
							}

#define MAX_FILES_TO_LOAD 256

							if( pos > 0 )
							{
								LocFile *file = CacheManagerFileGet( SLIB->cm, path->raw, TRUE );

								if( file != NULL )
								{
									char *mime = NULL;

									if( file->lf_Mime == NULL )
									{
										char *extension = FCalloc( 7, sizeof( char ) );
										int pos = 0;
										int max = 0;
										int ii = 0;

										for( ii = path->rawSize - 1; ii >= 0 ; ii-- )
										{
											if( path->raw[ ii ] == '.'  || max >= 5 )
											{
												pos = ii + 1;
												break;
											}
											max++;
										}

										for( ii = 0; ii < max; ii++ )
										{
											extension[ ii ] = path->raw[ pos++ ];
										}

										mime = StringDuplicate( MimeFromExtension( extension ) );

										FFree( extension );
									}
									else
									{
										mime = StringDuplicate( file->lf_Mime );
									}
									//FFree( extension );

									struct TagItem tags[] = {
											{ HTTP_HEADER_CONTENT_TYPE, (FULONG)  mime },
											{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
											{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "max-age = 3600" ) },
											{ TAG_DONE, TAG_DONE }
									};

									response = HttpNewSimple( HTTP_200_OK, tags );

									HttpSetContent( response, file->lf_Buffer, file->lf_FileSize );

									// write here and set data to NULL!!!!!
									// return response
									HttpWrite( response, sock );
									result = 200;

									response->content = NULL;
									response->sizeOfContent = 0;

									response->h_WriteType = FREE_ONLY;
								}
								else // file not found in cache
								{
									char *multipath = NULL, *pathTable[ MAX_FILES_TO_LOAD ];

									memset( pathTable, 0, MAX_FILES_TO_LOAD*sizeof(char *) );
									unsigned int pathSize = path->rawSize + 1;

									// split path 

									if( ( multipath = FCalloc( pathSize, sizeof( char ) ) ) != NULL )
									{
										memcpy( multipath, path->raw, pathSize );

										int entry = 0;
										pathTable[ entry ] = multipath;

										for( i = 0; i < pathSize; i++ )
										{
											if( multipath[ i ] == ';' )
											{
												multipath[ i ] = 0;
												pathTable[ ++entry ] = &(multipath[ i + 1 ] );
											}
										}

										BufString *bs = BufStringNewSize( 10240 );
										if( bs != NULL )
										{
											int resError = 404, ce = 0, de = 0, dl = 0;
											unsigned int ent1 = entry + 1, err = 0;

											//char *extension = FCalloc( 1, 16 );
											char extension[ 16 ];
											char *mime = NULL;
											memset( extension, 0, 16 );

											for( i = 0; i < ent1; i++ )
											{
												// Don't allow directory traversal
												if( strstr( pathTable[ i ], ".." ) )
												{
													continue;
												}

												// Let's find the file extension
												dl = strlen( pathTable[i] );
												for( ce = 0, de = 0; ce < dl; ce++ )
												{
													if( pathTable[i][ce] == '.' )
													{ 
														de = 1;
													}
													else if( de > 0 )
													{
														extension[de++-1] = pathTable[i][ce];
													}
												}

												if( mime != NULL )
												{
													FFree( mime );
													mime = NULL;
												}
												mime = StringDuplicate( MimeFromExtension( extension ) );

												err = ReadServerFile( request->uri, pathTable[ i ], bs, &result );

												if( result == 200 )
												{
													resError = 200;
												}
											}

											//FFree( extension );

											if( resError == 200 )
											{
												struct TagItem tags[] = {
														{ HTTP_HEADER_CONTENT_TYPE, (FULONG)  StringDuplicate( mime ) },
														{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
														{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "public, max-age=3600" ) },
														{ TAG_DONE, TAG_DONE }
												};

												response = HttpNewSimple( HTTP_200_OK, tags );

												HttpSetContent( response, bs->bs_Buffer, bs->bs_Size );

												LocFile* nlf = LocFileNewFromBuf( path->raw, bs );
												if( nlf != NULL )
												{
													nlf->lf_Mime = mime;

													DEBUG("[ProtocolHttp] File created %s size %lu\n", nlf->lf_Path, nlf->lf_FileSize );

													if( CacheManagerFilePut( SLIB->cm, nlf ) != 0 )
													{
														LocFileDelete( nlf );
													}
												}
												else
												{
													FFree( mime );
												}

												bs->bs_Buffer = NULL;

												// write here and set data to NULL!!!!!
												// retusn response
												HttpWrite( response, sock );

												//BufStringDelete( bs );

												result = 200;
											}
											// error, cannot open any file
											else
											{
												Log( FLOG_ERROR,"File do not exist '%s'\n", path->raw );

												struct TagItem tags[] = {
														{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
														{TAG_DONE, TAG_DONE}
												};	

												response = HttpNewSimple( HTTP_404_NOT_FOUND,  tags );

												result = 404;
											}

											//FFree( mime );
											BufStringDelete( bs );
										}
										FFree( multipath );
										DEBUG("multipath released\n");
									}
								}	// file not found in cache
							}
							else		// only one file
							{
								// test
								
								char *newUrl = NULL;
								FBOOL fromUrl = FALSE;

								if( strcmp( path->parts[ 0 ], "webclient" ) == 0 &&
									path->parts[ 1 ] != NULL && strcmp( path->parts[ 1 ], "index.html" ) == 0 
									&&
									
									SLIB->fcm->fcm_ClusterMaster )
								{
									//DEBUG("\n\n\n\n===========================\n\n");
									DEBUG("Checking connections, number sessions %d\n", SLIB->sl_USM->usm_SessionCounter );
							
									int minSessions = SLIB->sl_USM->usm_SessionCounter;
									ClusterNode *clusNode = SLIB->fcm->fcm_ClusterNodes;
						
									while( clusNode != NULL )
									{
										DEBUG("Checking sessions on node [%s] number %d min %d connection ptr %p NODEID: %lu\n", clusNode->cn_Address, clusNode->cn_UserSessionsCount, minSessions, clusNode->cn_Connection, clusNode->cn_ID );
										
										if( clusNode->cn_Connection != NULL && clusNode->cn_Connection->fc_Socket != NULL )
										{
											if( clusNode->cn_UserSessionsCount < minSessions )
											{
												DEBUG("New URL will be generated\n");
												if( clusNode->cn_Url != NULL )	// we cannot redirect to url which do not exist
												{
													fromUrl = TRUE;
													newUrl = clusNode->cn_Url;
												}
												else if( clusNode->cn_Address != NULL )
												{
													newUrl = clusNode->cn_Address;
												}
											}
										}
										else
										{
											DEBUG("Connection do not exist [%s]\n", clusNode->cn_Address );
										}
										clusNode = (ClusterNode *)clusNode->node.mln_Succ;
									}
								}
						
								DEBUG("[ProtocolHttp] getting login page for authmodule: %s\n", SLIB->sl_ActiveModuleName );

								//
								//
								//
						
								if( newUrl != NULL )
								{
									char redirect[ 1024 ];
									int redsize = 0;
							
									DEBUG("REDIRECT SET TO [%s]\n", newUrl );

									if( SLIB->fcm->fcm_SSLEnabled )
									{
/* redsize = snprintf( redirect, sizeof( redirect ), "<html><head><title>Redirecting...</title><meta http-equiv=\"refresh\" content=\"0;url=https://%s/webclient/index.html\"></head><body> \
 Attempting to redirect to <a href=\"https://%s/webclient/index.html\">https://%s</a>.</body></html>", newUrl, newUrl, newUrl ); */
										if( fromUrl == TRUE )
										{
											redsize = snprintf( redirect, sizeof( redirect ), "https://%s/webclient/index.html", newUrl );
										}
										else
										{
											redsize = snprintf( redirect, sizeof( redirect ), "https://%s:6502/webclient/index.html", newUrl );
										}
									}
									else
									{
								/* redsize = snprintf( redirect, sizeof( redirect ), "<html><head><title>Redirecting...</title><meta http-equiv=\"refresh\" content=\"0;url=http://%s:6502/webclient/index.html\"></head><body> \
 Attempting to redirect to <a href=\"http://%s:6502/webclient/index.html\">http://%s:6502</a>.</body></html>", newUrl, newUrl, newUrl ); */
										if( fromUrl == TRUE )
										{
											redsize = snprintf( redirect, sizeof( redirect ), "http://%s/webclient/index.html", newUrl );
										}
										else
										{
											redsize = snprintf( redirect, sizeof( redirect ), "http://%s:6502/webclient/index.html", newUrl );
										}
									}
							
									DEBUG("Redirected to: [%s]\n", redirect );

									struct TagItem tags[] = {
										{ HTTP_HEADER_CONTENT_TYPE, (FULONG) StringDuplicate("text/html") },
										{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
										{ HTTP_HEADER_LOCATION, (FULONG )StringDuplicateN( redirect, redsize ) },
										{ TAG_DONE, TAG_DONE}
									};

									//response = HttpNewSimple( HTTP_200_OK, tags );
									response = HttpNewSimple( HTTP_307_TEMPORARY_REDIRECT, tags );

									//HttpAddTextContent( response, redirect );
									HttpAddTextContent( response, "Redirection" );

									// write here and set data to NULL!!!!!
									// retusn response
									HttpWrite( response, sock );
									result = 200;
								}
								else
								{
									Path *base = PathNew( "resources" );
									Path* completePath = PathJoin( base, path );
									FBOOL freeFile = FALSE;

									if( completePath != NULL )
									{
										LocFile* file = NULL;

										//if( FRIEND_MUTEX_LOCK( &SLIB->sl_ResourceMutex ) == 0 )
										{
											char *decoded = UrlDecodeToMem( completePath->raw );
											if( SLIB->sl_CacheFiles == 1 )
											{
												Log( FLOG_DEBUG, "[ProtocolHttp] Read single file, first from cache %s\n", decoded );
												file = CacheManagerFileGet( SLIB->cm, decoded, FALSE );

												if( file == NULL )
												{
													// Don't allow directory traversal
													if( !strstr( decoded, ".." ) )
													{
														file = LocFileNew( decoded, FILE_READ_NOW | FILE_CACHEABLE );
													}

													if( file != NULL )
													{
														if( CacheManagerFilePut( SLIB->cm, file ) != 0 )
														{
															freeFile = TRUE;
														}
													}
												}
												else
												{
													struct stat attr;
													stat( decoded, &attr);

													// if file is new file, reload it
													
													if( attr.st_mtime != file->lf_Info.st_mtime )
													{
														Log( FLOG_DEBUG, "[ProtocolHttp] File will be reloaded\n");
														LocFileReload( file, decoded );
													}
												}
												
												if( file != NULL )
												{
													file->lf_InUse = 1;
												}
											}
											else
											{
												// Don't allow directory traversal
												if( !strstr( decoded, ".." ) )
												{
													file = LocFileNew( decoded, FILE_READ_NOW | FILE_CACHEABLE );
												}
												freeFile = TRUE;
											}
											FFree( decoded );
											DEBUG("Resource mutex released\n");
											//FRIEND_MUTEX_UNLOCK( &SLIB->sl_ResourceMutex );
										}
										Log( FLOG_DEBUG, "[ProtocolHttp] Return file content: file ptr %p\n", file );

										// Send reply
										if( file != NULL )
										{
											DEBUG("Check mime\n");
											char *mime = NULL;

											if(  file->lf_Buffer == NULL )
											{
												Log( FLOG_ERROR,"File is empty %s\n", completePath->raw );
											}

											if( file->lf_Mime == NULL )
											{
												DEBUG("GET single file : extension '%s'\n", completePath->extension );
												if( completePath->extension )
												{
													const char *t = MimeFromExtension( completePath->extension );
													mime = StringDuplicate( t );
												}
												else
												{
													mime = StringDuplicate( "text/plain" );
												}
												file->lf_Mime = StringDuplicate( mime );
											}
											else
											{
												mime = StringDuplicate( file->lf_Mime );
											}
											struct TagItem tags[] = {
												{ HTTP_HEADER_CONTENT_TYPE, (FULONG) mime },
												{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
												{ HTTP_HEADER_CACHE_CONTROL, (FULONG )StringDuplicate( "max-age = 3600" ) },
												{ TAG_DONE, TAG_DONE }
											};

											response = HttpNewSimple( HTTP_200_OK, tags );

											HttpSetContent( response, file->lf_Buffer, file->lf_FileSize );

											// write here and set data to NULL!!!!!
											// return response
											HttpWrite( response, sock );
											result = 200;

											response->content = NULL;
											response->sizeOfContent = 0;

											response->h_WriteType = FREE_ONLY;

											Log( FLOG_DEBUG, "[ProtocolHttp] File returned to caller, fsize %lu\n", file->lf_FileSize );

											//INFO("--------------------------------------------------------------%d\n", freeFile );
											if( freeFile == TRUE )
											{
												LocFileDelete( file );
											}
											else
											{
												file->lf_InUse = 0;
											}
										}
										else
										{
											// First try to get tinyurl
											char *hash = path->parts[0];
											SQLLibrary *sqllib  = SLIB->LibrarySQLGet( SLIB );

											char *url = FCalloc( 2048, sizeof(char) );
											
											//char url[ 2048 ]; 
											//memset( url, '\0', 2048 );
											int hasUrl = 0;
											if( sqllib != NULL )
											{
												char *qery = FMalloc( 1048 );
												//char qery[ 1048 ];
												qery[ 1024 ] = 0;
												sqllib->SNPrintF( sqllib, qery, 1024, "SELECT Source FROM FTinyUrl WHERE `Hash`=\"%s\"", hash ? hash : "-" );
												void *res = sqllib->Query( sqllib, qery );
												if( res != NULL )
												{
													char **row;
													if( ( row = sqllib->FetchRow( sqllib, res ) ) )
													{
														if( row[ 0 ] != NULL )
														{
															unsigned int len = strlen( row[ 0 ] ) + 1;
															if( len <= 2047 )
															{
																snprintf( url, 2048, "%.*s", len, row[ 0 ] );
																hasUrl = 1;
															}
														}
													}
													sqllib->FreeResult( sqllib, res );
												}
												SLIB->LibrarySQLDrop( SLIB, sqllib );
												
												FFree( qery );
											}
											
											// We have tinyurl!
											if( hasUrl )
											{
												Log( FLOG_DEBUG, "TinyURL found: %s\n", url );

												PathFree( path );
												PathFree( base );
												PathFree( completePath );

												UriFree( request->uri );
												request->uri = UriParse( url );
												if( request->uri->authority )
												{
													if( request->uri->authority->user )
													{
														FFree( request->uri->authority->user );
													}
													if( request->uri->authority->host )
													{
														FFree( request->uri->authority->host );
													}
													FFree( request->uri->authority );
												}
												if( request->uri->scheme )
												{
													FFree( request->uri->scheme );
												}
												request->uri->authority = NULL;
												request->uri->scheme = NULL;

												// Override raw query!
												FFree( request->uri->queryRaw );

												// Insert tinyurl source
												request->uri->queryRaw = StringDuplicateN( url, strlen( url ) );
												request->uri->redirect = TRUE;

												// Retry request with our new url
												FFree( url );
												goto partialRequest;
												
											}
											// No tiny url! Catch-all
											else
											{
												Log( FLOG_DEBUG, "[ProtocolHttp] File do not exist as real file, getting it via Modules\n");

												// Try to fall back on module
												// TODO: Make this behaviour configurable
												char *command = NULL;
												ListString *phpResp = NULL;

												int clen = strlen( uri->path->raw ) + 256;
												
												if( request->h_ContentType == HTTP_CONTENT_TYPE_APPLICATION_JSON )
												{
													/*
													HashmapElement *he = HttpGetPOSTParameter( request, "module" );
													if( he == NULL ) he = HashmapGet( request->query, "module" );

													if( he != NULL && he->data != NULL )
													{
														struct stat f;
														char runfile[ 512 ];
														snprintf( runfile, sizeof(runfile), "php \"php/catch_all.php\" \"%s\";", (char *)he->data );
					
														DEBUG("Run module: '%s'\n", runfile );
					
														if( stat( runfile, &f ) != -1 )
														{
															FULONG dataLength;
															DEBUG("MODRUNPHP %s\n", runfile );
															char *allArgsNew = GetArgsAndReplaceSession( *request, NULL );
															if( allArgsNew != NULL )
															{
																data = SLIB->sl_PHPModule->Run( SLIB->sl_PHPModule, runfile, allArgsNew, &dataLength );
																
																phpResp = ListStringNew();
																if( data != NULL )
																{
																	ListStringAdd( phpResp, data, dataLength );
																	ListStringJoin( phpResp );
																}
															}
														}
														else
														{
															FERROR("Module do not eixst %s\n", runfile );
														}
													}
													*/
													
													DEBUG("MODRUNPHP %s\n", "php/catch_all.php" );
													FBOOL isFile;
													char *allArgsNew = GetArgsAndReplaceSession( request, NULL, &isFile );
													if( allArgsNew != NULL )
													{
														int argssize = strlen( allArgsNew );
														char *runFile = FCalloc( ( argssize << 1 ) + 512 + strlen( uri->path->raw ), sizeof(char) );
														if( runFile != NULL )
														{
															int rawLength = strlen( uri->path->raw );
															
															strcpy( runFile, "php \"php/catch_all.php\" \"" );
															
															strcpy( runFile + 25, uri->path->raw );
															
															strcpy( runFile + 25 + rawLength, "\" \"" );
															
															char *src = allArgsNew;
															char *dst = runFile + strlen( runFile );
															
															while( *src != 0 )
															{
																if( *src == '"' )
																{
																	*dst = '\\';
																	dst++;
																}
																*dst = *src;
																
																src++;
																dst++;
															}
															*dst++ = '\"';
															
															//snprintf( runFile, argssize + 512, "php \"php/catch_all.php\" \"%s\";", allArgsNew );
															DEBUG("MODRUNPHP '%s'\n", runFile );
															
															phpResp = RunPHPScript( runFile );
														
															FFree( runFile );
														}
													}
													
													if( isFile )
													{
														//"file<!--separate-->%s"
														char *fname = allArgsNew + MODULE_FILE_CALL_STRING_LEN;
														remove( fname );
													}
													
													FFree( allArgsNew );
												}
												
												if( phpResp == NULL )
												{
													DEBUG("CatchALL 1621\n");
													if( ( command = FCalloc( clen, sizeof(char) ) ) != NULL )
													{
														snprintf( command, clen, "php \"php/catch_all.php\" \"%s\";", uri->path->raw ); 
													
														phpResp = RunPHPScript( command );
														
														FFree( command );
													}
												} // content-type json

												if( phpResp && phpResp->ls_Data != NULL && phpResp->ls_Size > 0 )
												{
													// Check header and remove from data
													char *cntype = CheckEmbeddedHeaders( phpResp->ls_Data, phpResp->ls_Size, "Content-Type" );
													char *code = CheckEmbeddedHeaders( phpResp->ls_Data, phpResp->ls_Size, "Status Code" );

													if( cntype != NULL )
													{
														phpResp->ls_Size = StripEmbeddedHeaders( &phpResp->ls_Data, phpResp->ls_Size );
													}

													struct TagItem tags[] = {
														{ HTTP_HEADER_CONTENT_TYPE, (FULONG)StringDuplicate( cntype ? cntype : "text/html" ) },
														{ HTTP_HEADER_CONNECTION,   (FULONG)StringDuplicate( "close" ) },
														{ TAG_DONE, TAG_DONE }
													};

													if( code != NULL )
													{
														char *pEnd;
														int errCode = -1;

														char *next;
														errCode = strtol ( code, &next, 10);
														if( ( next == code ) || ( *next != '\0' ) ) 
														{
															errCode = -1;
														}

														if( errCode == -1 )
														{
															response = HttpNewSimple( HTTP_200_OK, tags );
														}
														else
														{
															response = HttpNewSimple( errCode, tags );
														}

														Log( FLOG_DEBUG, "PHP catch returned err code: %d\n", errCode );
													}
													else
													{
														response = HttpNewSimple( HTTP_200_OK, tags );
													}

													char *resp = phpResp->ls_Data;
													if( resp != NULL )
													{
														const char *hsearche = "---http-headers-end---\n";
														const int hsearchLene = 23;

														char *tmp = NULL;
														if( ( tmp = strstr( resp, hsearche ) ) != NULL )
														{
															resp = tmp + 23;
														}
													}

													HttpWrite( response, sock );

													response->content = NULL;
													response->sizeOfContent = 0;

													response->h_WriteType = FREE_ONLY;

													SocketWrite( sock, resp, (FLONG)(phpResp->ls_Size - (resp - phpResp->ls_Data)) );

													if( cntype != NULL ) FFree( cntype );
													if( code != NULL ) FFree( code );

													result = 200;

													Log( FLOG_ERROR, "Module call returned bytes: %lu\n", phpResp->ls_Size );
													//bs->ls_Data = NULL; 
													ListStringDelete( phpResp );
												}
												else 
												{
													Log( FLOG_ERROR,"File do not exist (PHPCall)\n");

													struct TagItem tags[] = {
														{ HTTP_HEADER_CONNECTION, (FULONG)StringDuplicate( "close" ) },
														{ TAG_DONE, TAG_DONE }
													};	

													response = HttpNewSimple( HTTP_404_NOT_FOUND,  tags );

													result = 404;
												}
											}
											FFree( url );
										}
										Log( FLOG_DEBUG, "[ProtocolHttp] File delivered: %s\n", completePath->raw );

										PathFree( base );
										PathFree( completePath );
									}
									else
									{
										Log( FLOG_ERROR,"Cannot create completePath\n");
									}
								
								} // test redirection
							}		// one-many files read
						}
					}
				}
			} // else WEBDAV
		}

		HttpFreeRequest( request );

		// The response pointer might be -1 temporarily (because it might be
		// the result of immediate streaming that has no response). Set to null.

		if( response != NULL )
		{
			if( response != NULL && response->h_Stream == TRUE )
			{
				HttpFree( response );
				response = NULL;
			}
		}

		if( result != 101 )
		{
			sock->data = NULL;
		}
		PathFree( path );
		Log( FLOG_DEBUG, "HTTP parsed, returning response\n");

		return response;
	}
	// Winter cleaning
	HttpFreeRequest( request );
	Log( FLOG_DEBUG, "HTTP parsed1, returning response\n");
	return response;
}

