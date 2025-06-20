#pragma once

#include <unordered_map>
#include <functional>
#include <string>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "inference/ModelManager.h"

class Router {
public:
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
    }

    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();
        if (routes.count(key)) {
            return routes[key](request);
        }
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    void setupDatabaseRoutes(Database& db) {
        addRoute("POST", "/register", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];
            if (username.empty() || password.empty()) {
                return HttpResponse::makeErrorResponse(400, "Bad Request: missing fields");
            }
            if (db.registerUser(username, password)) {
                return HttpResponse::makeOkResponse("Register Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Register Failed!");
            }
        });

        addRoute("POST", "/login", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];
            if (username.empty() || password.empty()) {
                return HttpResponse::makeErrorResponse(400, "Bad Request: missing fields");
            }
            if (db.loginUser(username, password)) {
                return HttpResponse::makeOkResponse("Login Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Login Failed!");
            }
        });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes;
};
