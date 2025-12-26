# Requirements Document

## Introduction

This document specifies the requirements for refactoring the web server implementation to separate API endpoint handlers from the core web server functionality. The current implementation has all API handlers (SIP, WiFi, authentication, hardware testing, etc.) embedded within the web_server.c file, making it difficult to maintain and extend. This refactoring will improve code organization, maintainability, and testability by creating a dedicated API handler module.

## Glossary

- **Web_Server**: The HTTP/HTTPS server component responsible for serving HTML pages and routing requests
- **API_Handler**: A function that processes HTTP requests to API endpoints and generates JSON responses
- **API_Module**: A new source file (web_api.c/h) that contains all API endpoint handlers
- **URI_Handler**: An ESP-IDF HTTP server structure that maps URIs to handler functions
- **Authentication_Filter**: A function that validates session cookies and enforces access control
- **NVS**: Non-Volatile Storage used for persisting configuration data

## Requirements

### Requirement 1

**User Story:** As a firmware developer, I want API handlers separated from the web server code, so that I can maintain and extend API functionality independently

#### Acceptance Criteria

1. THE Web_Server SHALL delegate all API endpoint handling to the API_Module
2. THE API_Module SHALL contain all handler functions for endpoints matching the pattern "/api/*"
3. THE Web_Server SHALL retain only HTML page serving functionality and server lifecycle management
4. THE API_Module SHALL expose a registration function that accepts an httpd_handle_t parameter
5. WHEN the Web_Server starts, THE Web_Server SHALL invoke the API_Module registration function to register all API endpoints

### Requirement 2

**User Story:** As a firmware developer, I want authentication logic accessible to both modules, so that API handlers can enforce access control consistently

#### Acceptance Criteria

1. THE Authentication_Filter function SHALL remain in web_server.c
2. THE Web_Server SHALL expose the Authentication_Filter function through web_server.h
3. THE API_Module SHALL use the exposed Authentication_Filter for protected endpoints
4. THE Authentication_Filter SHALL validate session cookies for all non-public endpoints
5. WHEN an unauthenticated request reaches a protected endpoint, THE Authentication_Filter SHALL return HTTP 401 or redirect to login

### Requirement 3

**User Story:** As a firmware developer, I want helper functions shared between modules, so that I can avoid code duplication

#### Acceptance Criteria

1. WHERE email configuration functions exist, THE Web_Server SHALL move email_config_t structure and related functions to the API_Module
2. WHERE certificate loading functions exist, THE Web_Server SHALL retain certificate loading in web_server.c for server initialization
3. THE API_Module SHALL contain all JSON request parsing and response generation logic for API endpoints
4. THE Web_Server SHALL contain only HTML serving and HTTP-to-HTTPS redirect logic
5. THE API_Module SHALL include all cJSON operations for API responses

### Requirement 4

**User Story:** As a firmware developer, I want clear module boundaries, so that I can understand which file contains specific functionality

#### Acceptance Criteria

1. THE Web_Server SHALL contain fewer than 500 lines of code after refactoring
2. THE API_Module SHALL contain all API handler functions totaling approximately 2000-2500 lines
3. THE web_server.h header SHALL declare web_server_start, web_server_stop, and auth_filter functions
4. THE web_api.h header SHALL declare a single web_api_register_handlers function
5. THE API_Module SHALL organize handlers by functional area (SIP, WiFi, System, Auth, etc.)

### Requirement 5

**User Story:** As a firmware developer, I want the refactoring to preserve existing functionality, so that no API behavior changes

#### Acceptance Criteria

1. THE API_Module SHALL implement identical request handling logic as the original web_server.c
2. THE API_Module SHALL maintain all existing URI paths without modification
3. THE API_Module SHALL preserve all HTTP methods (GET, POST, DELETE) for each endpoint
4. THE API_Module SHALL maintain all authentication requirements for protected endpoints
5. WHEN the refactoring is complete, THE System SHALL respond to API requests identically to the original implementation

### Requirement 6

**User Story:** As a firmware developer, I want proper header file organization, so that compilation dependencies are clear

#### Acceptance Criteria

1. THE web_api.h header SHALL include only the registration function declaration
2. THE web_api.c file SHALL include all necessary headers for API functionality
3. THE web_server.c file SHALL include web_api.h to access the registration function
4. THE API_Module SHALL include web_server.h to access the Authentication_Filter
5. THE build system SHALL compile both modules without circular dependencies
