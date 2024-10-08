#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "web.h"

typedef void (*route_handler_t)(HTTPServer *s, HTTPRequest *r, int request_socket);

HTTPServer *StartWebServer(const char *ip, int port, int auto_search_dir) {
    if(port == 0)
        return NULL;

    HTTPServer *s = (HTTPServer *)malloc(sizeof(HTTPServer));

    if(ip != NULL)
        s->ip = strdup(ip);
    s->port = port;
    s->routes = create_map();
    s->routes->idx = 0;

    if ((s->socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        err_n_exit("[ x ] Error, Unable to create socket");

    s->address.sin_family = AF_INET;
    s->address.sin_port = htons(port);

    if(ip == NULL) {
        s->address.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_aton(ip, &s->address.sin_addr);
    }

    int reuse = 1;
    if(setsockopt(s->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        err_n_exit("setsockopt(SO_REUSEADDR) failed");

    if (bind(s->socket, (struct sockaddr *)&s->address, sizeof(s->address)) < 0)
        err_n_exit("[ x ] Error, Unable to bind socket");

    return s;
}

int AddRoute(HTTPServer *s, const char *route, void *fn) {
    if(s == NULL || s->routes == NULL)
        return 0;

    Multi_T *route_fn = (Multi_T *)malloc(sizeof(Multi_T));
    route_fn->name = strdup(route);
    route_fn->fn = fn;

    s->routes->keys[s->routes->idx] = route_fn;
    s->routes->idx++;
    s->routes->keys = (void **)realloc(s->routes->keys, (sizeof(void *) * s->routes->idx) + 1);
    return 1;
}

int Add404Page(HTTPServer *s, const char *path) {
    // TODO: check if file exists
    s->err_404_filepath = strdup(path);

    return 1;
}

void StartListener(HTTPServer *s) {
    if (listen(s->socket, 999) < 0)
        return;

    printf("Server is listening on port %d\n", s->port);

    int request_socket = 0, addrlen = sizeof(s->address);
    while(1) {
        if((request_socket = accept(s->socket, (struct sockaddr *)&s->address, (socklen_t*)&addrlen)) < 0)
            continue;

        // Skip request if failed (avoid crashes)
        pthread_t tid;
        void **arr = (void **)malloc(sizeof(void *) * 2);
        arr[0] = (void *)s;
        arr[1] = (void *)request_socket;

        pthread_create(&tid, NULL, (void *)thread_req, (void *)arr);
    }
}

void *thread_req(void **a) {
    HTTPServer *s = (HTTPServer *)a[0];
    int sock = (int)a[1];

    ParseAndCheckForRoute(s, sock);
    close(sock);
}

int isRouteValid(HTTPServer *s, char *data) {
    str *route = string(data);
    route->Strip(route);
    if(route->IsEmpty(route) > -1)
        return -1;

    if(route == NULL)
        return -1;

    if(route->idx == 2) {
        route->data = strdup("/index");
        route->idx = strlen("/index") + 1;
    }

    for(int i = 0; i < s->routes->idx; i++) {
        if((Key *)s->routes->keys[i] == NULL)
            break;

        if(!strcmp((char *)((Key *)s->routes->keys[i])->name, route->data))
            return i;
    }

    for(int i = 0; i < s->routes->idx; i++) {
        if((Key *)s->routes->keys[i] == NULL)
            break;
            
        if( (long)route->StartsWith(route, (char *)((Key *)s->routes->keys[i])->name) )
            return i;
    }

    return -1;
}

void *ParseAndCheckForRoute(HTTPServer *s, int request_socket) {
    char buffer[4096];
    memset(buffer, '\0', 4096);
    read(request_socket, buffer, 4095);

    HTTPRequest *r = ParseHTTPTraffic(buffer);
    if(r == NULL || r->route == NULL)
        return NULL;

    int chk = 0;
    Map *headers = create_map();
    headers->Utils(headers, __ADD_KEY, "Content-Type", "text/html; charset=UTF-8");
    headers->Utils(headers, __ADD_KEY, "Connection", "close");
    if(r->body != NULL && r->route != NULL) {
        chk = isRouteValid(s, r->route->data);

        if(!strcmp(r->request_type->data, "POST"))
            get_post_queries(s, r);

        if(chk > -1) {
            ((route_handler_t)((Key *)s->routes->keys[chk])->value)(s, r, request_socket);
            return 0;
        }
        
        if(s->err_404_filepath != NULL)
            SendRawResponse(s, request_socket, OK, headers, s->err_404_filepath, NULL);
    } else {
        if(s->err_404_filepath != NULL)
            SendRawResponse(s, request_socket, OK, headers, s->err_404_filepath, NULL);
    }
}

Map *get_post_queries(HTTPServer *s, HTTPRequest *r) {
    Map *queries = create_map();
    char **args = (char **)r->body->Split(r->body, "&");

    int count = count_arr(args);
    if(count < 1)
        return NULL;

    for(int i = 0; i < count; i++) {
        str *query = string(args[i]);
        char **query_args = query->Split(query, "=");
        int arg_count = count_arr(query_args);

        if(arg_count > 1)
            queries->Utils(queries, __ADD_KEY, query_args[0], query_args[1]);

        free(query);
        free(query_args);
    }

    free(args);
    r->queries = queries;
    return queries;
}

int retrieve_get_parameter(HTTPServer *s, HTTPRequest *r) {
    if(!strstr(r->route->data, "?"))
        return 0;


    Map *queries = create_map();
    char **args = (char **)r->route->Split(r->route, "?");
    str *parameters = string(args[1]);

    args = parameters->Split(parameters, "&");
    int count = count_arr(args);

    if(count > 0) {
        for(int i = 0; i < count; i++) {
            str *para = string(args[i]);
            char **para_args = para->Split(para, "=");
            queries->Utils(queries, __ADD_KEY, para_args[0], para_args[1]);
        }
        r->queries = queries;
        return 1;
    }

    return 0;
}

HTTPRequest *ParseHTTPTraffic(const char *data) {
    str *raw_data = string(data);

    char **lines = raw_data->Split(raw_data, "\n");
    int line_count = count_arr(lines);

    Map *headers = create_map();
    str *body = string(NULL);
    HTTPRequest *r = (HTTPRequest *)malloc(sizeof(HTTPRequest));

    if(line_count == 0)
        return NULL;

    str *web_info = string(lines[0]);
    char **arg = web_info->Split(web_info, " ");

    int c = 0;

    r->request_type = string(arg[0]);
    if(count_arr(arg) < 1)
        return NULL;
            
    r->route = string(arg[1]);

    free(web_info);

    int stop = 0;
    for(int i = 1; i < line_count; i++)
    {
        if(!strcmp(lines[i], "") || !strcmp(lines[i], "2be")) {
            stop = 1;
            continue;
        } else if(!strcmp(lines[i], "0")) {
            break;
        }

        str *line = string(lines[i]);
        char **args = line->Split(line, ":");
        int arg_count = count_arr(args);

        line->Strip(line);
        if(strcmp(line->data, "") == 0) {
            stop = 1;
            continue;
        }

        if(arg_count >= 2 && !stop) {
            str *value = string(lines[i]);
            value->ReplaceString(value, args[0], "");
            
            headers->Utils(headers, __ADD_KEY, args[0], value->data);
            free(value);
        } else {
            body->AppendString(body, lines[i]);
        }

        free(line);
        free(args);
    }

    free(raw_data);
    free(lines);
    free(arg);
    r->body = body;
    r->headers = headers;
    return r;
}

void SendResponse(HTTPServer *s, int request_socket, StatusCode_T code, Map *headers, const char *html_file, Map *vars) { 
    str *req_data = string("HTTP/1.1 200 OK\r\n");

    if(headers != NULL) {
        for(int i = 0; i < headers->idx; i++) {
            req_data->AppendString(req_data, (char *)((Key *)headers->keys[i])->name);
            req_data->AppendString(req_data, ": ");
            req_data->AppendString(req_data, (char *)((Key *)headers->keys[i])->value);
            req_data->AppendString(req_data, "\r\n");
        }
    }

    if(html_file != NULL) {
        if(!access(html_file, F_OK) == 0) {
            write(request_socket, req_data->data, strlen(req_data->data));
            free(req_data); 
            return;
        }

        cFile *file = Openfile(html_file);
        file->Read(file);
        if(vars != NULL) {
            str *raw_html_data = string(file->data);

            for(int i = 0; i < vars->idx; i++) {
                if((Key *)vars->keys[i] == NULL)
                    break;

                // Variable Replacing
                if(strstr(raw_html_data->data, (char *)((Key *)vars->keys[i])->name))
                    raw_html_data->ReplaceString(raw_html_data, (char *)((Key *)vars->keys[i])->name, (char *)((Key *)vars->keys[i])->value);
            }

            req_data->AppendString(req_data, raw_html_data->data);
            write(request_socket, req_data->data, strlen(req_data->data));

            free(raw_html_data);
            free(req_data); 

            free(file);
            return;
        }

        req_data->AppendString(req_data, file->data);
        write(request_socket, req_data->data, strlen(req_data->data));
        free(req_data);
        free(file);
        return;
    }

    write(request_socket, req_data->data, strlen(req_data->data));
    free(req_data); 
}


void SendRawResponse(HTTPServer *s, int request_socket, StatusCode_T code, Map *headers, const char *html_file, Map *vars) { 
    str *req_data = string("HTTP/1.1 200 OK\r\n");

    /* Add Headers */
    if(headers != NULL) {
        for(int i = 0; i < headers->idx; i++) {
            req_data->AppendString(req_data, (char *)((Key *)headers->keys[i])->name);
            req_data->AppendString(req_data, ": ");
            req_data->AppendString(req_data, (char *)((Key *)headers->keys[i])->value);
            req_data->AppendString(req_data, "\r\n");
        }
    }
    req_data->AppendString(req_data, "\r\n");

    if(html_file == NULL) {
        write(request_socket, req_data->data, strlen(req_data->data));
        free(req_data);
        return;
    }

    // Open file and check for errs
    cFile *html = Openfile(html_file);
    if(!html->fd) {
        printf("[ x ] Unable to find HTML File.... %s\n", html_file);
        write(request_socket, req_data->data, strlen(req_data->data));
        free(req_data);
        free(html);
        return;
    }

    // Read file and parse for functions or append the one line file
    html->Read(html);
    str *raw_html = string(html->data);
    if(raw_html->CountChar(raw_html, '\n') > 0) {
        Arr *a = Array(raw_html->Split(raw_html, "\n"));
        for(int i = 0; i < a->idx - 1; i++) {
            if(a->arr[i] == NULL)
                break;

            // parse include_css() and include_html() lines
            str *line = string(a->arr[i]);
            if(strstr(line->data, "include_css(") || strstr(line->data, "include_html(")) {
                req_data->AppendString(req_data, parse_include_line(line, i));
                req_data->AppendString(req_data, "\r\n");
                continue;
            }
            
            req_data->AppendString(req_data, line->data);
            req_data->AppendString(req_data, "\r\n");
        }
    } else {
        req_data->AppendString(req_data, raw_html->data);
    }

    // Check for if no var is provided
    if(vars == NULL) {
        write(request_socket, req_data->data, strlen(req_data->data));
        free(req_data);
        free(html);
        return;
    }

    write(request_socket, req_data->data, strlen(req_data->data));
    free(req_data);
}

char *parse_include_line(str *line, int line_num) {
    str *file_data = string(NULL);

    // Check end of line syntax
    line->Strip(line);
    if(!(long)line->EndsWith(line, ");")) {
        printf("[ x ] Error, Invalid syntax. Missing ';' at the end of line %d\n", line_num);
        return NULL;
    }

    // Check for ( position and get substr
    int ch = line->FindCharAt(line, '(', 1);
    str *raw_args = string(line->GetSubstr(line, ch, line->idx));

    // Detect quotes to remove
    int quote_count = raw_args->CountChar(raw_args, '"');
    if(quote_count > 0) {
        for(int i = 0; i < quote_count; i++)
            raw_args->ReplaceChar(raw_args, '"', ' ');
    }
    char RmChars[] = {'(', ')', ';'};
    for(int i = 0; i < 3; i++)
        raw_args->ReplaceChar(raw_args, RmChars[i], ' ');
    raw_args->Strip(raw_args);

    // Check for multiple arguments and add file data
    if(strstr(raw_args->data, ",")) {
        Arr *args = Array(raw_args->Split(raw_args, ","));
        for(int i = 0; i < args->idx; i++) {
            // open files and append
            str *file_name = string(args->arr[i]);
            file_name->Strip(file_name);
            cFile *current_file = Openfile(file_name->data);
            current_file->Read(current_file);
            file_data->AppendString(file_data, current_file->data);
            free(current_file);
            free(file_name);
        }
    } else {
        cFile *file = Openfile(raw_args->data);
        file->Read(file);
        file_data->AppendString(file_data, file->data);
        free(file);

    }

    return file_data->data;
}

int CloseServer(HTTPServer *s) {
    close(s->socket);
}