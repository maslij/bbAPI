# tAPI - API Reference

This document provides a comprehensive reference of the tAPI REST endpoints.

## Base URL

By default, tAPI runs on:
```
http://localhost:8080
```

You can specify a different port with:
```
./build/tAPI --port <PORT>
```

For network access, tAPI binds to all interfaces (0.0.0.0).

## Authentication

Currently, tAPI does not implement authentication.

## Response Format

All responses are in JSON format unless otherwise specified (such as image data or HTML pages).

## Endpoints

### General

#### Get API Status
- **GET** `/`
- **Response**: Text message indicating API is running
- **Example**:
  ```bash
  curl http://localhost:8080/
  ```

### Stream Management

#### List All Streams
- **GET** `/api/streams`
- **Response**: JSON array of all streams
- **Example**:
  ```bash
  curl http://localhost:8080/api/streams
  ```
- **Response Example**:
  ```json
  [
    {
      "id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a",
      "name": "Big Buck Bunny",
      "source": "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4",
      "type": "file",
      "status": "running",
      "width": 320,
      "height": 240,
      "fps": 30
    }
  ]
  ```

#### Get Stream by ID
- **GET** `/api/streams/{id}`
- **Response**: JSON object with stream details
- **Example**:
  ```bash
  curl http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a
  ```
- **Response Example**:
  ```json
  {
    "id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a",
    "name": "Big Buck Bunny",
    "source": "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4",
    "type": "file",
    "status": "running",
    "width": 320,
    "height": 240,
    "fps": 30
  }
  ```

#### Create a New Stream
- **POST** `/api/streams`
- **Content-Type**: `application/json`
- **Request Body**:
  ```json
  {
    "source": "SOURCE",          // Camera index, file path, or RTSP URL
    "type": "TYPE",              // "camera", "file", or "rtsp"
    "name": "NAME",              // Human-readable name
    "autoStart": true/false      // Optional, default: false
  }
  ```
- **Response**: JSON object with created stream ID
- **Example**:
  ```bash
  curl -X POST http://localhost:8080/api/streams \
    -H "Content-Type: application/json" \
    -d '{"source":"0","type":"camera","name":"Webcam","autoStart":true}'
  ```
- **Response Example**:
  ```json
  {
    "id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"
  }
  ```

#### Start a Stream
- **POST** `/api/streams/{id}/start`
- **Response**: JSON object with success status
- **Example**:
  ```bash
  curl -X POST http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/start
  ```
- **Response Example**:
  ```json
  {
    "success": true,
    "id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"
  }
  ```

#### Stop a Stream
- **POST** `/api/streams/{id}/stop`
- **Response**: JSON object with success status
- **Example**:
  ```bash
  curl -X POST http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/stop
  ```
- **Response Example**:
  ```json
  {
    "success": true,
    "id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"
  }
  ```

#### Delete a Stream
- **DELETE** `/api/streams/{id}`
- **Response**: JSON object with success status
- **Example**:
  ```bash
  curl -X DELETE http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a
  ```
- **Response Example**:
  ```json
  {
    "success": true,
    "id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"
  }
  ```

### Stream Consumption

#### Get a Single Frame
- **GET** `/api/streams/{id}/frame`
- **Response**: JPEG image
- **Content-Type**: `image/jpeg`
- **Example**:
  ```bash
  curl http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/frame --output frame.jpg
  ```
- Can be embedded in HTML:
  ```html
  <img src="http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/frame" />
  ```

#### Get Stream Data
- **GET** `/api/streams/{id}/stream`
- **Response**: Single frame in multipart format
- **Content-Type**: `multipart/x-mixed-replace; boundary=frame`
- **Example**:
  ```bash
  curl http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/stream --output stream.jpg
  ```

#### Get Embed Page
- **GET** `/api/streams/{id}/embed`
- **Response**: HTML page with embedded auto-refreshing stream and embedding instructions
- **Content-Type**: `text/html`
- **Example**: Navigate to:
  ```
  http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/embed
  ```

### Alarm System

#### Get Current Alarms
- **GET** `/api/streams/{streamId}/alarms`
- **Description**: Retrieves all current alarms with bounding boxes and object images
- **Response**: JSON array of alarm events
- **Example**:
  ```bash
  curl http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/alarms
  ```
- **Response Example**:
  ```json
  {
    "stream_id": "a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a",
    "pipeline_id": "pipeline_5678",
    "alarm_events": [
      {
        "message": "ALARM: detection (person_123) confidence: 0.95 - Detected person with confidence 0.95 for 6 seconds",
        "object_id": "person_123",
        "object_class": "person",
        "confidence": 0.95,
        "timestamp": 1234567890,
        "bbox": {
          "x": 100,
          "y": 100,
          "width": 50,
          "height": 50
        },
        "image_data": "base64_encoded_image"
      }
    ],
    "count": 1
  }
  ```

#### Get Alarm Events (Legacy)
- **GET** `/api/streams/{streamId}/alarm_events`
- **Description**: Legacy endpoint for retrieving alarm events, maintained for backward compatibility
- **Response**: JSON array of alarm events in the same format as `/alarms`
- **Example**:
  ```bash
  curl http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/alarm_events
  ```

#### Force Test Alarms (Debug)
- **POST** `/api/debug/force_alarm`
- **Description**: Debug endpoint to create test alarms for development and testing
- **Response**: JSON object indicating success and affected stream
- **Example**:
  ```bash
  curl -X POST http://localhost:8080/api/debug/force_alarm
  ```
- **Response Example**:
  ```json
  {
    "status": "success",
    "message": "Forced 2 alarms for stream a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"
  }
  ```
- **Generated Alarms**:
  1. Person Detection:
     - 95% confidence
     - Bounding box: (100, 150, 80, 200)
     - Includes cropped image from frame if available
  2. Car Detection:
     - 90% confidence
     - Bounding box: (200, 200, 120, 100)
     - Includes car-like shape drawn on frame if available

**Important Notes**:
- Retrieving alarms via these endpoints does not remove them from storage
- The system maintains the last 100 alarms in a FIFO queue
- Alarms are automatically removed when:
  1. New alarms push them out of the 100-event limit
  2. The EventAlarm component is reset
  3. The stream or pipeline is stopped/deleted
- Alarms include object images when available, encoded in base64 format

## Common Use Cases

### Create and View a Camera Stream

1. Create a stream from camera:
   ```bash
   curl -X POST http://localhost:8080/api/streams \
     -H "Content-Type: application/json" \
     -d '{"source":"0","type":"camera","name":"Webcam","autoStart":true}'
   ```

2. Get the stream ID from the response:
   ```json
   {"id":"a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"}
   ```

3. View the stream in a browser:
   ```
   http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/embed
   ```

### Create and View a Video File Stream

1. Create a stream from file:
   ```bash
   curl -X POST http://localhost:8080/api/streams \
     -H "Content-Type: application/json" \
     -d '{"source":"https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4","type":"file","name":"Big Buck Bunny","autoStart":true}'
   ```

2. Get the stream ID from the response:
   ```json
   {"id":"a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"}
   ```

3. View the stream in a browser:
   ```
   http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/embed
   ```

### Embedding a Stream in a Web Application

```html
<script>
  // Auto-refresh image to simulate streaming
  function refreshStream() {
    const img = document.getElementById('stream-id');
    img.src = 'http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/frame?t=' + new Date().getTime();
    setTimeout(refreshStream, 1000);  // Refresh every second
  }
  window.onload = refreshStream;
</script>

<!-- Image that will be auto-refreshed -->
<img id="stream-id" src="http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/frame" />
```

## Error Handling

All API errors return an appropriate HTTP status code along with a JSON error message:

```json
{
  "error": "Error message"
}
```

Common error codes:
- `400 Bad Request`: Invalid input parameters
- `404 Not Found`: Stream not found
- `500 Internal Server Error`: Server-side error

## Stream States

Streams can be in one of the following states:
- `created`: Stream created but not started
- `running`: Stream currently running
- `stopped`: Stream stopped
- `error`: Stream encountered an error

## Notes for LLM Integration

When working with tAPI through an LLM (Large Language Model) interface:

1. Always generate complete, valid JSON for all POST requests.
2. Use proper content type headers: `-H "Content-Type: application/json"`.
3. For stream URLs, replace `localhost` with the server's IP address when accessing from other devices.
4. Remember that stream IDs are UUIDs and must be used exactly as returned by the API.
5. For camera sources, use "0" for the default camera or the camera index for other cameras.
6. For file sources, use either a local file path or a URL to a video file.
7. For RTSP sources, use a valid RTSP URL.

## Example Conversation with an LLM

```
User: Can you help me create a video stream from my webcam using tAPI?

LLM: I'll help you create a webcam stream using tAPI.

First, let's create the stream:

curl -X POST http://localhost:8080/api/streams \
  -H "Content-Type: application/json" \
  -d '{"source":"0","type":"camera","name":"My Webcam","autoStart":true}'

This should return a stream ID like:
{"id":"a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a"}

Now you can view your webcam stream by opening this URL in your browser:
http://localhost:8080/api/streams/a007d3b3-c0f2-485e-acdc-cb0bbbcf6b1a/embed

To access it from another device on your network, replace "localhost" with your computer's IP address.
```

## Vision Pipeline System

### Overview

The Vision Pipeline system allows processing video streams through a chain of vision components. The system:

1. Supports creating and managing pipelines with multiple nodes
2. Allows activating a specific pipeline for a stream
3. Processes each frame from the stream through the active pipeline
4. Supports different component types: sources, detectors, trackers, classifiers, and sinks

### Pipeline Creation and Management

#### Creating a Pipeline

To create a pipeline for a stream:

```
POST /api/streams/{streamId}/pipelines
```

Request body:
```json
{
  "name": "My Detection Pipeline",
  "nodes": [
    {
      "id": "camera_node_1",
      "componentId": "camera_feed",
      "position": {"x": 100, "y": 100},
      "connections": ["detector_node_1"],
      "config": {
        "source": "",
        "device": 0,
        "fps": 30
      }
    },
    {
      "id": "detector_node_1",
      "componentId": "object_detector",
      "position": {"x": 300, "y": 100},
      "connections": ["annotated_node_1"],
      "config": {
        "confidence": 0.6,
        "model": "faster_rcnn"
      }
    },
    {
      "id": "annotated_node_1",
      "componentId": "annotated_stream",
      "position": {"x": 500, "y": 100},
      "connections": [],
      "config": {
        "show_labels": true,
        "show_bounding_boxes": true
      }
    }
  ]
}
```

#### Activating a Pipeline

To activate a pipeline for a stream:

```
POST /api/streams/{streamId}/pipelines/{pipelineId}/activate
```

This sets the pipeline as the active one for processing frames from the stream. Only one pipeline can be active per stream.

#### Deactivating a Pipeline

To deactivate a pipeline:

```
POST /api/streams/{streamId}/pipelines/{pipelineId}/deactivate
```

### Pipeline Processing Flow

When a frame is captured from a stream with an active pipeline:

1. The system identifies source nodes in the pipeline (typically `camera_feed` components)
2. Starting from each source node, it follows the connections to create a processing flow
3. Each node applies its specific processing based on its component type
4. Output from each node is passed to its connected nodes
5. The final processed frame (typically from a sink node like `annotated_stream`) is returned

### Available Component Types

#### Source Components

| Component ID | Description | Outputs |
|--------------|-------------|---------|
| `camera_feed` | Source video frames from a camera or stream | `image` |

#### Detector Components

| Component ID | Description | Inputs | Outputs |
|--------------|-------------|--------|---------|
| `object_detector` | Detects objects using a deep learning model | `image` | `detections` |
| `face_detector` | Detects faces in images | `image` | `faces` |
| `motion_detector` | Detects motion in video frames | `image` | `motion_regions` |

#### Tracker Components

| Component ID | Description | Inputs | Outputs |
|--------------|-------------|--------|---------|
| `object_tracker` | Tracks objects across frames | `image`, `detections` | `tracked_objects` |
| `face_tracker` | Tracks faces across frames | `image`, `faces` | `tracked_faces` |

#### Classifier Components

| Component ID | Description | Inputs | Outputs |
|--------------|-------------|--------|---------|
| `object_classifier` | Classifies detected objects | `image`, `detections` | `classified_objects` |
| `face_recognizer` | Recognizes faces from a database | `image`, `faces` | `recognized_faces` |

#### Geometry Components

| Component ID | Description | Inputs | Outputs |
|--------------|-------------|--------|---------|
| `polygon_crossing` | Detects objects crossing polygon boundaries | `tracked_objects` | `crossing_events` |
| `zone_occupancy` | Counts objects within defined zones | `tracked_objects` | `occupancy_counts` |

#### Sink Components

| Component ID | Description | Inputs |
|--------------|-------------|--------|
| `annotated_stream` | Creates an annotated video stream | `image`, `detections`, `tracked_objects`, etc. |
| `event_logger` | Logs detection events | `detections`, `tracked_objects`, etc. |

### Implementation Notes

- Pipelines are validated before activation to ensure they're properly configured
- The system automatically identifies source components by name even if custom component IDs are used
- If no source component is found, the first node is treated as the source
- The system handles cycles in the pipeline graph to prevent infinite loops
- During initial integration, the validation is intentionally lenient to facilitate development

### Example: Create and Activate a Pipeline

1. Create a pipeline:

```
POST /api/streams/stream_123/pipelines
```
```json
{
  "name": "Detection Pipeline",
  "nodes": [
    {
      "id": "source_1",
      "componentId": "camera_feed",
      "position": {"x": 100, "y": 100},
      "connections": ["detector_1"]
    },
    {
      "id": "detector_1",
      "componentId": "object_detector",
      "position": {"x": 300, "y": 100},
      "connections": ["sink_1"]
    },
    {
      "id": "sink_1",
      "componentId": "annotated_stream",
      "position": {"x": 500, "y": 100},
      "connections": []
    }
  ]
}
```

2. Activate the pipeline:

```
POST /api/streams/stream_123/pipelines/pipeline_456/activate
```

3. The system will now process each frame from stream_123 using the pipeline, applying object detection and returning the annotated stream.

## REST API Endpoints

### Streams

[Existing documentation...] 