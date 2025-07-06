"""
Pydantic models for High-Quality Audio Extraction API
Optimized for fingerprinting and audio recognition workflows
"""

from pydantic import BaseModel, HttpUrl, Field
from typing import List, Optional, Dict, Any
from enum import Enum


class AudioFormat(str, Enum):
    """Supported audio output formats"""
    wav = "wav"
    mp3 = "mp3" 
    flac = "flac"
    m4a = "m4a"
    aac = "aac"


class ProcessingQuality(str, Enum):
    """Audio processing quality levels"""
    minimal = "minimal"        # Minimal processing - preserve original characteristics
    standard = "standard"      # Light normalization and cleanup
    enhanced = "enhanced"      # More aggressive noise reduction and enhancement


class LogLevel(str, Enum):
    """Logging levels"""
    DEBUG = "DEBUG"
    INFO = "INFO"
    WARNING = "WARNING"
    ERROR = "ERROR"


class ExtractAudioRequest(BaseModel):
    """Request model for high-quality audio extraction"""
    url: HttpUrl = Field(..., description="URL to download and extract audio from")
    
    # Audio quality settings
    output_format: AudioFormat = Field(
        AudioFormat.wav, 
        description="Output audio format (WAV recommended for fingerprinting)"
    )
    sample_rate: Optional[int] = Field(
        None, 
        description="Target sample rate (None=preserve original, 44100 recommended for fingerprinting)", 
        ge=8000, 
        le=192000
    )
    preserve_original_quality: bool = Field(
        True, 
        description="Preserve original audio characteristics for fingerprinting"
    )
    
    # Processing options
    processing_quality: ProcessingQuality = Field(
        ProcessingQuality.minimal, 
        description="Level of audio processing to apply"
    )
    normalize_volume: bool = Field(
        True, 
        description="Apply gentle volume normalization"
    )
    remove_dc_offset: bool = Field(
        True, 
        description="Remove DC offset from audio"
    )
    high_pass_filter: bool = Field(
        True, 
        description="Apply light high-pass filter to remove sub-audible content"
    )
    
    # Output settings
    channels: Optional[int] = Field(
        None, 
        description="Target number of channels (None=preserve original, 1=mono, 2=stereo)", 
        ge=1, 
        le=8
    )
    bit_depth: int = Field(
        16, 
        description="Audio bit depth", 
        choices=[16, 24, 32]
    )
    
    # Download settings
    prefer_original_codec: bool = Field(
        False, 
        description="Prefer original codec over conversion when possible"
    )
    max_duration: Optional[int] = Field(
        None, 
        description="Maximum duration in seconds (None=no limit)", 
        ge=1, 
        le=7200
    )


class ExtractionStatus(str, Enum):
    """Audio extraction status values"""
    queued = "queued"
    downloading = "downloading"
    processing = "processing"
    completed = "completed"
    failed = "failed"


class AudioFileInfo(BaseModel):
    """Information about an extracted audio file"""
    filename: str = Field(..., description="Name of the output file")
    file_path: str = Field(..., description="Path to the output file")
    file_size: int = Field(..., description="File size in bytes")
    duration: Optional[float] = Field(None, description="Audio duration in seconds")
    format: str = Field(..., description="Audio format")
    sample_rate: int = Field(..., description="Sample rate in Hz")
    channels: int = Field(..., description="Number of audio channels")
    bit_depth: int = Field(..., description="Audio bit depth")
    
    # Quality metrics
    peak_level: Optional[float] = Field(None, description="Peak audio level in dB")
    rms_level: Optional[float] = Field(None, description="RMS audio level in dB")
    dynamic_range: Optional[float] = Field(None, description="Dynamic range in dB")
    
    # Processing info
    processing_applied: List[str] = Field(default=[], description="List of processing steps applied")
    original_format: Optional[str] = Field(None, description="Original source format")
    conversion_quality: Optional[str] = Field(None, description="Quality of format conversion")


class ExtractAudioResponse(BaseModel):
    """Response model for audio extraction"""
    success: bool = Field(..., description="Whether extraction was successful")
    task_id: str = Field(..., description="Unique task identifier")
    status: ExtractionStatus = Field(..., description="Extraction status")
    message: str = Field(..., description="Status message")
    
    # Output information
    output_file: Optional[AudioFileInfo] = Field(None, description="Generated audio file info")
    processing_time: Optional[float] = Field(None, description="Total processing time in seconds")
    
    # Source information
    source_title: Optional[str] = Field(None, description="Title of source content")
    source_duration: Optional[float] = Field(None, description="Duration of source content")
    source_quality: Optional[str] = Field(None, description="Quality of source audio")
    
    # Processing details
    processing_method: str = Field("minimal-hq-extraction", description="Processing method used")
    quality_preserved: bool = Field(True, description="Whether original quality was preserved")
    fingerprint_optimized: bool = Field(True, description="Whether optimized for fingerprinting")
    
    # Error handling
    error: Optional[str] = Field(None, description="Error message if failed")
    warnings: List[str] = Field(default=[], description="Processing warnings")


class TaskStatusResponse(BaseModel):
    """Response model for task status queries"""
    task_id: str = Field(..., description="Task identifier")
    status: ExtractionStatus = Field(..., description="Current status")
    progress: float = Field(0.0, description="Progress percentage (0-100)", ge=0, le=100)
    message: str = Field(..., description="Current status message")
    current_step: str = Field(..., description="Current processing step")
    
    output_file: Optional[AudioFileInfo] = Field(None, description="Generated file info")
    error: Optional[str] = Field(None, description="Error message if failed")
    warnings: List[str] = Field(default=[], description="Processing warnings")
    
    # Timestamps
    started_at: Optional[str] = Field(None, description="Task start time (ISO format)")
    completed_at: Optional[str] = Field(None, description="Task completion time (ISO format)")
    estimated_completion: Optional[str] = Field(None, description="Estimated completion time")


class ProcessingCapability(BaseModel):
    """Information about processing capabilities"""
    name: str = Field(..., description="Capability name")
    available: bool = Field(..., description="Whether capability is available")
    description: str = Field(..., description="Description of capability")
    quality_impact: str = Field(..., description="Impact on audio quality")


class HealthResponse(BaseModel):
    """Health check response"""
    status: str = Field("healthy", description="Service health status")
    version: str = Field("5.0.0", description="API version")
    
    dependencies: Dict[str, Any] = Field(
        default_factory=dict, 
        description="Dependency status and capabilities"
    )
    
    processing_capabilities: List[ProcessingCapability] = Field(
        default_factory=list, 
        description="Available processing capabilities"
    )
    
    recommended_settings: Dict[str, Any] = Field(
        default_factory=lambda: {
            "fingerprinting": {
                "format": "wav",
                "sample_rate": 44100,
                "channels": 2,
                "bit_depth": 16,
                "processing_quality": "minimal"
            }
        },
        description="Recommended settings for different use cases"
    )


class ErrorResponse(BaseModel):
    """Error response model"""
    error: str = Field(..., description="Error type")
    message: str = Field(..., description="Error message")
    details: Optional[Dict[str, Any]] = Field(None, description="Additional error details")
    task_id: Optional[str] = Field(None, description="Related task ID if applicable")
    
    # Suggestions for resolution
    suggested_settings: Optional[Dict[str, Any]] = Field(
        None, 
        description="Suggested settings to resolve the issue"
    )
    retry_with_fallback: bool = Field(
        False, 
        description="Whether retrying with fallback settings might succeed"
    )


class SystemInfo(BaseModel):
    """System information and capabilities"""
    version: str = Field(..., description="API version")
    supported_formats: List[AudioFormat] = Field(..., description="Supported output formats")
    processing_qualities: List[ProcessingQuality] = Field(..., description="Available quality levels")
    
    # Technical capabilities
    max_file_size: Optional[int] = Field(None, description="Maximum file size in bytes")
    max_duration: Optional[int] = Field(None, description="Maximum duration in seconds")
    supported_sample_rates: List[int] = Field(
        default=[8000, 16000, 22050, 44100, 48000, 96000, 192000],
        description="Supported sample rates"
    )
    
    # Performance info
    average_processing_speed: Optional[float] = Field(
        None, 
        description="Average processing speed (realtime factor)"
    )
    concurrent_limit: int = Field(4, description="Maximum concurrent extractions")


# Commonly used type aliases for convenience
AudioExtractionRequest = ExtractAudioRequest  # Alias for backward compatibility