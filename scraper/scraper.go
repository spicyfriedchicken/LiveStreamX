// heavily commented because I am bad at golang, sorry.

package scraper

import (
	"fmt"
	"sync"
)
import "os/exec"

func main() {
	
	videoIDs := make(chan string, 100)

	const maxWorkers = 10

	func startWorkerPool(videoIDs chan string, wg *sync.WaitGroup) {
		for i:= 0; i < maxWorkers; i++ {
			wg.Add(1) // Tell the waitgroup we have spawned a new worker
			go func(workerID int) { // create a lambda that takes in a unique workerId
				defer wg.Done() // When we finish this lambda, tell WaitGroup this worker has exited
				for videoID := range videoIDs { // Iterate through all videoIDs in the channel
					processVideo(videoID) // One worker will execute this function
					// In a range loop through a channel, all workers
				}
			}(i) // immediate invocation of the unique lambda with a copy of a unique workerId
		}
	}

	// yt-dlp specific commands, consult the docs for any other use-cases
	func downloadVideo(videoID string) error {
		cmd := exec.Command("yt-dlp",
		"-f", "bestvideo+bestaudio",
		"--merge-output-format", "mp4",
		"--output", fmt.Sprintf("%s.%%(ext)s", videoID),
		"--write-info-json",
		fmt.Sprintf("https://www.youtube.com/watch?v=%s", videoID),
	)
		return cmd.Run()
	}

	// exponential backoff, function capture as 1st argument, if err, time.Sleep for 2^i
	func retry_with_backoff(fn func() error, maxRetries int) error {
		var err error
		for i:= 0; i < maxRetries; i++ {
			err = fn()
			if err == nil {
				return nil
			}
			time.Sleep(1 * time.Second * time.Duration(2<<i)) // exponential backoff
		}
		return err
	}

	type VideoMetadata struct {
		Title       string `json:"title"`
		ID          string `json:"id"`
		UploadDate  string `json:"upload_date"`
		Duration    int    `json:"duration"`
		Format      string `json:"format"`
	}

	func uploadToS3(filename string, bucket string) error {
		cfg, err := config.LoadDefaultConfig(context.TODO())
		if err != nil {
			return err
		}

		client := s3.NewFromConfig(cfg)
		f, er := os.Open(filename)
		if err != nil {
			return err
		}

		defer f.Close()

		_, err = client.PutObject(context.TODO(), &s3.PutObjectInput{
			Bucket: aws.String(bucket),
			Key:    aws.String(filename),
			Body:   f,
		})
		return err;
	}

	func processVideo(videoID string) {
		retryAttempts := 5
		err := retry(func() error {
			return downlodVideo(videoID)
		}, retryAttempts) // arg 2
		if err != nil {
			fmt.Printf("could not download video: %s\n", err)
		}

		uploadToS3(videoID + ".mp4", "LiveStreamX/mp4/")
		uploadToS3(videoID + ".mp4", "LiveStreamX/json/")
	}

	func main() {
		videoIDs := make(chan string, 100)
		var wg sync.WaitGroup

		go func() {
			defer close(videoIDs)
			for _, id := range getVideoIdsFromChannel("niiyan1216") {
				videoIDs <- id
			}
		}()

		StartWorkerPool(videoIDs, &wg)
		wg.Wait()
	}

}
