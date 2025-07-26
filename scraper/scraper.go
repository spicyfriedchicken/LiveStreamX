package main

import (
	"bytes"
	"context"
	"fmt"
	"github.com/joho/godotenv"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/service/s3"
)

const (
	maxWorkers = 10
	maxVideos  = 100
)

func downloadVideo(videoID string) error {
	fmt.Printf("[downloader] downloading %s\n", videoID)
	cmd := exec.Command("yt-dlp",
		"-f", "bestvideo+bestaudio",
		"--merge-output-format", "mp4",
		"--no-keep-video",
		"--paths", "tmp",
		"--output", fmt.Sprintf("%s.%%(ext)s", videoID),
		"--write-info-json",
		fmt.Sprintf("https://www.youtube.com/watch?v=%s", videoID),
	)

	var stderr bytes.Buffer
	cmd.Stderr = &stderr

	if err := cmd.Run(); err != nil {
		fmt.Printf("[downloader] yt-dlp failed for %s: %v\n", videoID, err)
		fmt.Println("[downloader] stderr:\n", stderr.String())
		return err
	}
	return nil
}

func retry(fn func() error, maxRetries int) error {
	var err error
	for i := 0; i < maxRetries; i++ {
		err = fn()
		if err == nil {
			return nil
		}
		wait := 1 * time.Second * time.Duration(1<<i)
		fmt.Printf("[retry] failed attempt %d, retrying in %v...\n", i+1, wait)
		time.Sleep(wait)
	}
	return err
}

func uploadToS3(filename string, key string, bucketName string) error {
	cfg, err := config.LoadDefaultConfig(context.TODO())
	if err != nil {
		return err
	}

	client := s3.NewFromConfig(cfg)

	f, err := os.Open(filename)
	if err != nil {
		return err
	}
	defer f.Close()

	fmt.Printf("[uploader] uploading %s -> %s\n", filename, key)
	_, err = client.PutObject(context.TODO(), &s3.PutObjectInput{
		Bucket: aws.String(bucketName),
		Key:    aws.String(key),
		Body:   f,
	})
	return err
}

func processVideo(videoID string, bucket string, workerID int) {
	fmt.Printf("[worker %d] processing video id: %s\n", workerID, videoID)

	err := retry(func() error {
		return downloadVideo(videoID)
	}, 5)

	if err != nil {
		fmt.Printf("[worker %d] failed to download %s: %v\n", workerID, videoID, err)
		return
	}

	mp4Path := fmt.Sprintf("tmp/%s.mp4", videoID)
	jsonPath := fmt.Sprintf("tmp/%s.info.json", videoID)

	for i := 0; i < 10; i++ {
		if _, err := os.Stat(mp4Path); err == nil {
			break
		}
		fmt.Printf("[worker %d] waiting for %s...\n", workerID, mp4Path)
		time.Sleep(2 * time.Second)
	}

	if _, err := os.Stat(mp4Path); os.IsNotExist(err) {
		fmt.Printf("[worker %d] mp4 not found: %s\n", workerID, mp4Path)
		return
	}
	if _, err := os.Stat(jsonPath); os.IsNotExist(err) {
		fmt.Printf("[worker %d] json not found: %s (skipping upload)\n", workerID, jsonPath)
		return
	}

	err = uploadToS3(mp4Path, "mp4/"+videoID+".mp4", bucket)
	if err != nil {
		fmt.Printf("[worker %d] failed to upload mp4: %v\n", workerID, err)
		return
	}

	err = uploadToS3(jsonPath, "json/"+videoID+".info.json", bucket)
	if err != nil {
		fmt.Printf("[worker %d] failed to upload json: %v\n", workerID, err)
		return
	}

	os.Remove(mp4Path)
	os.Remove(jsonPath)

	fmt.Printf("[worker %d] finished %s (cleaned up)\n", workerID, videoID)
}

func getVideoIdsFromChannel(channel string) []string {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, "yt-dlp",
		"--flat-playlist",
		"--playlist-end", "100",
		"--print", "%(id)s",
		fmt.Sprintf("https://www.youtube.com/@%s/videos", channel))

	output, err := cmd.Output()
	if err != nil {
		fmt.Printf("[extractor] yt-dlp error: %v\n", err)
		return nil
	}

	lines := strings.Split(string(output), "\n")
	var ids []string
	for _, line := range lines {
		if trimmed := strings.TrimSpace(line); trimmed != "" {
			ids = append(ids, trimmed)
		}
	}
	fmt.Printf("[extractor] got %d video ids from @%s\n", len(ids), channel)
	return ids
}

func startWorkerPool(videoIDs chan string, wg *sync.WaitGroup, bucket string) {
	for i := 0; i < maxWorkers; i++ {
		wg.Add(1)
		go func(workerID int) {
			defer wg.Done()
			for videoID := range videoIDs {
				processVideo(videoID, bucket, workerID)
			}
		}(i)
	}
}

func main() {
	fmt.Println("stage 1")
	err := godotenv.Load()
	if err != nil {
		log.Fatal("error loading .env file")
	}
	fmt.Println("stage 2")

	bucket := os.Getenv("BUCKET_NAME")
	if bucket == "" {
		log.Fatal("bucket_name not set in .env")
	}
	fmt.Println("stage 3")

	videoIDs := make(chan string, 100)
	var wg sync.WaitGroup
	fmt.Println("stage 4")

	go func() {
		defer close(videoIDs)
		count := 0
		for _, id := range getVideoIdsFromChannel(os.Getenv("CHANNEL")) {
			if count >= 100 {
				fmt.Println(" reached 100-video limit")
				break
			}
			fmt.Printf(" queued video id: %s\n", id)
			videoIDs <- id
			count++
		}
	}()
	fmt.Println("stage 5")

	startWorkerPool(videoIDs, &wg, bucket)
	fmt.Println("stage 6")

	wg.Wait()
	fmt.Println("stage 7")
	fmt.Println("stage - all workers finished.")
}
