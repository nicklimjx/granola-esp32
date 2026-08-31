package main

import (
	"embed"
	"log"
	"net/http"
)

//go:embed dashboard.html app.js game.mjs audio
var assets embed.FS

func routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /{$}", func(w http.ResponseWriter, _ *http.Request) {
		page, err := assets.ReadFile("dashboard.html")
		if err != nil {
			http.Error(w, "dashboard unavailable", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write(page)
	})
	mux.Handle("GET /app.js", http.FileServer(http.FS(assets)))
	mux.Handle("GET /game.mjs", http.FileServer(http.FS(assets)))
	mux.Handle("GET /audio/", http.FileServer(http.FS(assets)))
	return mux
}

func main() {
	log.Println("Granola dashboard listening on http://localhost:8080")
	log.Fatal(http.ListenAndServe("localhost:8080", routes()))
}
