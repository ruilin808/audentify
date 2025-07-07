#!/bin/bash
# docker-scripts.sh - Management scripts for Audentify

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_color() {
    printf "${1}${2}${NC}\n"
}

# Function to check if Docker is running
check_docker() {
    if ! docker info >/dev/null 2>&1; then
        print_color $RED "❌ Docker is not running. Please start Docker and try again."
        exit 1
    fi
}

# Function to build all services
build_all() {
    print_color $BLUE "🏗️ Building all Audentify services..."
    check_docker
    
    # Build services
    docker-compose build --parallel
    
    if [ $? -eq 0 ]; then
        print_color $GREEN "✅ All services built successfully!"
    else
        print_color $RED "❌ Build failed!"
        exit 1
    fi
}

# Function to start all services
start_all() {
    print_color $BLUE "🚀 Starting all Audentify services..."
    check_docker
    
    # Start services in detached mode
    docker-compose up -d
    
    if [ $? -eq 0 ]; then
        print_color $GREEN "✅ All services started successfully!"
        print_color $YELLOW "📋 Service URLs:"
        echo "  🌐 Main Application: http://localhost:8081"
        echo "  🎵 Vocal Removal API: http://localhost:8000"
        echo "  🎼 Audio Fingerprinting API: http://localhost:8080"
        echo ""
        print_color $YELLOW "📊 To view logs: docker-compose logs -f"
        print_color $YELLOW "🛑 To stop services: docker-compose down"
    else
        print_color $RED "❌ Failed to start services!"
        exit 1
    fi
}

# Function to stop all services
stop_all() {
    print_color $BLUE "🛑 Stopping all Audentify services..."
    docker-compose down
    
    if [ $? -eq 0 ]; then
        print_color $GREEN "✅ All services stopped successfully!"
    else
        print_color $RED "❌ Failed to stop some services!"
        exit 1
    fi
}

# Function to restart all services
restart_all() {
    print_color $BLUE "🔄 Restarting all Audentify services..."
    stop_all
    start_all
}

# Function to view logs
view_logs() {
    service=${1:-""}
    if [ -z "$service" ]; then
        print_color $BLUE "📋 Viewing logs for all services..."
        docker-compose logs -f
    else
        print_color $BLUE "📋 Viewing logs for $service..."
        docker-compose logs -f $service
    fi
}

# Function to show service status
status() {
    print_color $BLUE "📊 Service Status:"
    docker-compose ps
    
    echo ""
    print_color $BLUE "🔍 Health Checks:"
    
    # Check each service
    services=("audentify-app:8081/test" "vocal-removal:8000/" "audio-fingerprinting:8080/health")
    for service in "${services[@]}"; do
        name=${service%:*}
        url="http://localhost:${service#*:}"
        
        if curl -f -s "$url" >/dev/null 2>&1; then
            print_color $GREEN "  ✅ $name - Healthy"
        else
            print_color $RED "  ❌ $name - Unhealthy"
        fi
    done
}

# Function to clean up Docker resources
cleanup() {
    print_color $BLUE "🧹 Cleaning up Docker resources..."
    
    # Stop and remove containers
    docker-compose down -v
    
    # Remove unused images
    docker image prune -f
    
    # Remove unused volumes (be careful with this)
    print_color $YELLOW "⚠️ Do you want to remove unused volumes? (y/N)"
    read -r response
    if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]; then
        docker volume prune -f
        print_color $GREEN "✅ Volumes cleaned"
    fi
    
    print_color $GREEN "✅ Cleanup completed!"
}

# Function to show help
show_help() {
    echo "🎵 Audentify Docker Management Script"
    echo ""
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  build     Build all services"
    echo "  start     Start all services"
    echo "  stop      Stop all services"
    echo "  restart   Restart all services"
    echo "  logs      View logs (optionally specify service name)"
    echo "  status    Show service status and health"
    echo "  cleanup   Clean up Docker resources"
    echo "  help      Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 build                    # Build all services"
    echo "  $0 start                    # Start all services"
    echo "  $0 logs                     # View all logs"
    echo "  $0 logs vocal-removal       # View logs for vocal-removal service"
    echo "  $0 status                   # Check service status"
    echo ""
}

# Main script logic
case "${1:-help}" in
    build)
        build_all
        ;;
    start)
        start_all
        ;;
    stop)
        stop_all
        ;;
    restart)
        restart_all
        ;;
    logs)
        view_logs $2
        ;;
    status)
        status
        ;;
    cleanup)
        cleanup
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_color $RED "❌ Unknown command: $1"
        echo ""
        show_help
        exit 1
        ;;
esac