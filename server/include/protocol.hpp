#pragma once

#include <string>
#include <Poco/Net/HTTPRequestHandler.h> // For HTTP methods if you want to use Poco's constants
#include <Poco/Net/HTTPResponse.h>    // For HTTP status codes

// --- Base API Path ---
// All API endpoints will be prefixed with this.
// Example: http://localhost:12345/api/v1/
const std::string API_BASE_PATH = "/api/v1";

// --- Custom HTTP Headers ---
namespace HttpHeaders {
    const std::string AUTH_TOKEN = "X-Auth-Token";         // For session token
    const std::string FILE_CHECKSUM = "X-File-Checksum";   // SHA256 checksum for uploads/downloads
    const std::string FILE_LAST_MODIFIED = "X-File-Last-Modified"; // Unix timestamp for file
    const std::string FILE_RELATIVE_PATH = "X-File-Relative-Path"; // Often used with multipart/form-data uploads
} // namespace HttpHeaders


// --- API Endpoints and Methods ---
namespace Endpoints {
    // User Management
    const std::string REGISTER        = API_BASE_PATH + "/users/register";     // POST
    const std::string LOGIN           = API_BASE_PATH + "/users/login";        // POST
    const std::string LOGOUT          = API_BASE_PATH + "/users/logout";       // POST (requires token)
    const std::string USER_ME         = API_BASE_PATH + "/users/me";           // GET (requires token, gets current user info)
    // const std::string DELETE_USER  = API_BASE_PATH + "/users/{user_id}"; // DELETE (admin or self, requires token) - Placeholder for path param

    // File & Directory Operations (relative to user's home or a shared root)
    // For these, the actual path might be a query parameter or part of the URL path itself if design allows.
    // Option 1: Path as a query parameter (simpler for varied paths)
    //   e.g., /files/upload?path=documents/report.pdf
    // Option 2: Path embedded in URL (RESTful, but can be tricky with deep paths/special chars)
    //   e.g., /files/content/documents/report.pdf

    // Using query parameter "path" for simplicity and flexibility here.
    const std::string FILES_UPLOAD    = API_BASE_PATH + "/files/upload";       // POST (multipart/form-data, path in header or form field)
    const std::string FILES_DOWNLOAD  = API_BASE_PATH + "/files/download";     // GET (path as query param, e.g., ?path=doc.txt)
    const std::string FILES_METADATA  = API_BASE_PATH + "/files/metadata";     // GET (path as query param)
    const std::string FILES_LIST      = API_BASE_PATH + "/files/list";         // GET (path as query param, defaults to root)
    const std::string FILES_MKDIR     = API_BASE_PATH + "/files/mkdir";        // POST (JSON body with "path")
    const std::string FILES_DELETE    = API_BASE_PATH + "/files/delete";       // DELETE (path as query param or JSON body)
    const std::string FILES_RENAME    = API_BASE_PATH + "/files/rename";       // POST (JSON body with "old_path", "new_path")
    const std::string FILES_MOVE      = API_BASE_PATH + "/files/move";         // POST (JSON body with "source_path", "dest_path")

    // Synchronization
    // Client sends its manifest, server responds with actions needed.
    const std::string SYNC_MANIFEST   = API_BASE_PATH + "/sync/manifest";      // POST (JSON body with client file states)
                                                                            // Response: JSON body with sync operations

    // Sharing & Permissions (Example - design can vary greatly)
    const std::string SHARED_CREATE_STORAGE = API_BASE_PATH + "/shared/storage"; // POST (JSON body: {"name": "project_alpha"})
    // const std::string SHARED_STORAGE_DETAILS = API_BASE_PATH + "/shared/storage/{storage_name}"; // GET
    const std::string SHARED_GRANT_ACCESS = API_BASE_PATH + "/shared/access";    // POST (JSON body: {"storage_name", "username", "permission"})
    const std::string SHARED_REVOKE_ACCESS = API_BASE_PATH + "/shared/access";   // DELETE (JSON body or query params)
    // const std::string FILES_SHARE      = API_BASE_PATH + "/files/share";      // POST (JSON: {"path", "target_user", "permission"})
} // namespace Endpoints


// --- JSON Payload Keys (for request/response bodies that are JSON) ---
// These are similar to the previous protocol.hpp but used within HTTP JSON bodies.
namespace JsonKeys {
    // Common
    const std::string STATUS = "status";         // "success", "error" (in JSON response bodies)
    const std::string MESSAGE = "message";       // Accompanying message
    const std::string DATA = "data";             // Generic data payload in response

    // User
    const std::string USERNAME = "username";
    const std::string PASSWORD = "password";
    const std::string TOKEN = "token";           // In login response body
    const std::string USER_ID = "user_id";
    const std::string HOME_DIR = "home_dir";

    // File/Dir
    const std::string PATH = "path";
    const std::string OLD_PATH = "old_path";
    const std::string NEW_PATH = "new_path";
    const std::string SOURCE_PATH = "source_path";
    const std::string DEST_PATH = "dest_path";
    const std::string NAME = "name";
    const std::string IS_DIRECTORY = "is_directory";
    const std::string SIZE = "size";
    const std::string LAST_MODIFIED = "last_modified"; // Unix timestamp
    const std::string CHECKSUM = "checksum";
    const std::string LISTING = "listing";           // Array of file info

    // Sync
    const std::string CLIENT_FILES = "client_files"; // Array for sync manifest
    const std::string SYNC_OPERATIONS = "sync_operations";
    const std::string SYNC_ACTION_TYPE = "sync_action_type";
    const std::string RELATIVE_PATH = "relative_path"; // Used within sync structures

    // Sharing
    const std::string STORAGE_NAME = "storage_name";
    const std::string TARGET_USER = "target_user";
    const std::string PERMISSION = "permission";     // "r", "rw"
} // namespace JsonKeys


// --- HTTP Status Code Mappings (Informational) ---
/*
    Common HTTP Status Code Usage:

    Successful Operations:
    - 200 OK: General success for GET, PUT, PATCH, or sometimes POST if no new resource created.
              Response body often contains requested data or status message.
    - 201 Created: Successful POST that resulted in a new resource being created.
                   Response body might contain details of the new resource, and
                   'Location' header might point to the new resource's URI.
    - 202 Accepted: Request accepted for processing, but processing not complete (e.g., async task).
    - 204 No Content: Successful request but no data to return in the body (e.g., successful DELETE).

    Client Errors:
    - 400 Bad Request: General client-side error (e.g., malformed JSON, missing parameters).
                       Response body should contain error details.
    - 401 Unauthorized: Authentication is required and has failed or has not yet been provided.
                        (Client needs to send valid credentials/token).
    - 403 Forbidden: Authenticated user does not have permission to access the resource/perform action.
    - 404 Not Found: The requested resource could not be found on the server.
    - 405 Method Not Allowed: The HTTP method used is not supported for the requested resource.
    - 409 Conflict: Request could not be completed due to a conflict with the current state of the resource
                    (e.g., trying to create a user that already exists, edit conflict).
    - 422 Unprocessable Entity: Server understands the content type, and syntax is correct, but it was
                                unable to process the contained instructions (semantic errors).

    Server Errors:
    - 500 Internal Server Error: A generic error message, given when an unexpected condition was
                                 encountered and no more specific message is suitable.
    - 503 Service Unavailable: Server is not ready to handle the request (e.g., overloaded, down for maintenance).
*/

// --- Content Types ---
namespace ContentTypes {
    const std::string APPLICATION_JSON = "application/json; charset=utf-8";
    const std::string MULTIPART_FORM_DATA = "multipart/form-data";
    const std::string APPLICATION_OCTET_STREAM = "application/octet-stream"; // For binary file downloads
    const std::string TEXT_PLAIN = "text/plain; charset=utf-8";
} // namespace ContentTypes


// --- File Upload (using multipart/form-data) ---
/*
   For uploading files (e.g., to Endpoints::FILES_UPLOAD):
   The client should send a POST request with Content-Type: multipart/form-data.
   The form should contain:
   1. A file part:
      - name: "file" (or a consistent name you choose)
      - filename: the original name of the file
      - Content-Type: (e.g., image/jpeg, application/pdf) - client can try to guess or use octet-stream
   2. Optional form fields for metadata (alternatively, use custom headers):
      - name: "relativePath": (e.g., "documents/projectX/report.pdf") - path where to store on server
      - name: "checksum": (e.g., "sha256_hex_value")
      - name: "lastModified": (Unix timestamp)

   Example using cURL:
   curl -X POST \
     -H "X-Auth-Token: your_session_token" \
     -F "file=@/path/to/local/file.txt" \
     -F "relativePath=folder/on/server/file.txt" \
     http://localhost:12345/api/v1/files/upload

   Server-side (using Poco::Net::HTMLForm to parse multipart):
   Poco::Net::HTMLForm form(request, request.stream());
   if (form.find("file") != form.end()) {
       const Poco::Net::PartSource* partSource = static_cast<Poco::Net::PartSource*>(form.getPart("file"));
       std::string relativePath = form.get("relativePath", ""); // Get from form field
       // Or get relativePath from HttpHeaders::FILE_RELATIVE_PATH header
       // Stream partSource->stream() to fileManager.upload_file(...)
   }
*/

// --- File Download ---
/*
   For downloading files (e.g., from Endpoints::FILES_DOWNLOAD?path=...):
   The server will respond with:
   - HTTP Status: 200 OK
   - Content-Type: application/octet-stream (or a more specific MIME type if known)
   - Content-Disposition: attachment; filename="actual_filename.ext" (suggests download)
   - X-File-Checksum: (checksum of the file) - Custom Header
   - X-File-Last-Modified: (timestamp) - Custom Header
   - Body: Raw binary content of the file.
*/