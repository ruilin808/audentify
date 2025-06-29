"""
Pydantic models for API requests and responses
"""

from pydantic import BaseModel, HttpUrl, Field
from typing import List, Optional
from enum import Enum


class AudioFormat(str, Enum):
    """Supported audio output formats"""
    wav = "wav"
    mp3 = "mp3" 
    flac = "flac"
    m4a = "m4a"
    aac = "aac"


class LogLevel(str, Enum):
    """Logging levels"""
    DEBUG = "DEBUG"
    INFO = "INFO"
    WARNING = "WARNING"
    ERROR = "ERROR"


class ProcessUrlRequest(BaseModel):
    """Request model for processing a single URL"""
    url: HttpUrl = Field(..., description="URL to download and process")
    output_format: AudioFormat = Field(AudioFormat.wav, description="Output audio format")
    sample_rate: int = Field(44100, description="Audio sample rate", ge=8000, le=192000)
    keep_intermediate: bool = Field(False, description="Keep intermediate downloaded files")
    channels: int = Field(2, description="Audio channels for video extraction", ge=1, le=8)
    bitrate: Optional[str] = Field(None, description="Audio bitrate for video extraction (e.g., '320k')")


class ProcessBatchRequest(BaseModel):
    """Request model for batch processing URLs"""
    urls: List[HttpUrl] = Field(..., description="List of URLs to process", min_items=1, max_items=50)
    output_format: AudioFormat = Field(AudioFormat.wav, description="Output audio format")
    sample_rate: int = Field(44100, description="Audio sample rate", ge=8000, le=192000)
    keep_intermediate: bool = Field(False, description="Keep intermediate downloaded files")
    channels: int = Field(2, description="Audio channels for video extraction", ge=1, le=8)
    bitrate: Optional[str] = Field(None, description="Audio bitrate for video extraction")


class ProcessingStatus(str, Enum):
    """Processing status values"""
    queued = "queued"
    downloading = "downloading"
    extracting = "extracting"
    separating = "separating"
    completed = "completed"
    failed = "failed"


class AudioFileInfo(BaseModel):
    """Information about a processed audio file"""
    filename: str = Field(..., description="Name of the output file")
    file_path: str = Field(..., description="Path to the output file")
    file_size: int = Field(..., description="File size in bytes")
    duration: Optional[float] = Field(None, description="Audio duration in seconds")
    format: str = Field(..., description="Audio format")
    sample_rate: int = Field(..., description="Sample rate")


class ProcessUrlResponse(BaseModel):
    """Response model for URL processing"""
    success: bool = Field(..., description="Whether processing was successful")
    task_id: str = Field(..., description="Unique task identifier")
    status: ProcessingStatus = Field(..., description="Current processing status")
    message: str = Field(..., description="Status message")
    output_files: List[AudioFileInfo] = Field(default=[], description="Generated audio files")
    processing_time: Optional[float] = Field(None, description="Total processing time in seconds")
    error: Optional[str] = Field(None, description="Error message if failed")


class ProcessBatchResponse(BaseModel):
    """Response model for batch processing"""
    success: bool = Field(..., description="Whether batch processing was successful")
    task_id: str = Field(..., description="Unique batch task identifier")
    total_urls: int = Field(..., description="Total number of URLs to process")
    completed: int = Field(0, description="Number of URLs completed")
    failed: int = Field(0, description="Number of URLs failed")
    status: ProcessingStatus = Field(..., description="Overall batch status")
    output_files: List[AudioFileInfo] = Field(default=[], description="All generated audio files")
    failed_urls: List[str] = Field(default=[], description="URLs that failed to process")
    processing_time: Optional[float] = Field(None, description="Total batch processing time")


class TaskStatusResponse(BaseModel):
    """Response model for task status queries"""
    task_id: str = Field(..., description="Task identifier")
    status: ProcessingStatus = Field(..., description="Current status")
    progress: float = Field(0.0, description="Progress percentage (0-100)", ge=0, le=100)
    message: str = Field(..., description="Current status message")
    output_files: List[AudioFileInfo] = Field(default=[], description="Generated files so far")
    error: Optional[str] = Field(None, description="Error message if failed")
    started_at: Optional[str] = Field(None, description="Task start time (ISO format)")
    completed_at: Optional[str] = Field(None, description="Task completion time (ISO format)")


class HealthResponse(BaseModel):
    """Health check response"""
    status: str = Field("healthy", description="Service health status")
    version: str = Field("1.0.0", description="API version")
    dependencies: dict = Field(default_factory=dict, description="Dependency status")


class ErrorResponse(BaseModel):
    """Error response model"""
    error: str = Field(..., description="Error type")
    message: str = Field(..., description="Error message")
    details: Optional[dict] = Field(None, description="Additional error details")
    task_id: Optional[str] = Field(None, description="Related task ID if applicable")