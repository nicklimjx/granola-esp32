package main

import (
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestServesDashboardAndJavaScript(t *testing.T) {
	server := httptest.NewServer(routes())
	defer server.Close()

	for _, test := range []struct {
		path, contentType, contains string
	}{
		{"/", "text/html", "Connect board"},
		{"/app.js", "text/javascript", "navigator.bluetooth"},
		{"/game.mjs", "text/javascript", "export class Game"},
	} {
		response, err := http.Get(server.URL + test.path)
		if err != nil {
			t.Fatal(err)
		}
		body, readErr := io.ReadAll(response.Body)
		response.Body.Close()
		if readErr != nil {
			t.Fatal(readErr)
		}
		if response.StatusCode != http.StatusOK || !strings.Contains(response.Header.Get("Content-Type"), test.contentType) || !strings.Contains(string(body), test.contains) {
			t.Fatalf("GET %s: status=%d content-type=%q body=%q", test.path, response.StatusCode, response.Header.Get("Content-Type"), body)
		}
	}
}

func TestMissingAssetIsNotFound(t *testing.T) {
	request := httptest.NewRequest(http.MethodGet, "/missing", nil)
	response := httptest.NewRecorder()
	routes().ServeHTTP(response, request)
	if response.Code != http.StatusNotFound {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusNotFound)
	}
}
