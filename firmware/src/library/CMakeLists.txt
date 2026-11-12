add_library(echo_library STATIC
    album_order.c
    block_cache.c
    covers.c
    lyrics.c
    favorites.c
    library.c
    library_browse.c
    library_content.c
    library_gain.c
    library_view.c
    music_fingerprint.c
    sort_initial.c
    sort_key.c
    sort_fold_table.c
    spoken_path.c
)
target_include_directories(echo_library PUBLIC include)
target_link_libraries(echo_library
    PUBLIC echo_core
    PRIVATE echo_options echo_compact
)

add_library(echo_library_scan STATIC
    builder_sort.c
    builder_store.c
    cover_build.c
    cue_sheet.c
    library_builder.c
    library_builder_albums.c
    library_builder_encode.c
    library_builder_model.c
    library_builder_paths.c
    library_builder_rank.c
    library_scan.c
    library_scan_chapters.c
    library_scan_cue.c
    mp4_chapters.c
    playlist_reader.c
    replay_gain.c
    sort_fold_table.c
    sort_key.c
    tags_ape.c
    tags_asf.c
    tags_genres.c
    tags_id3.c
    tags_mp4.c
    tags_mpeg.c
    tags_riff.c
    tags_picture.c
    tags_vorbis.c
    track_fields.c
    tag_duration.c
    track_tags.c
)
target_include_directories(echo_library_scan PUBLIC include)
target_link_libraries(echo_library_scan
    PUBLIC echo_core echo_image echo_library
    PRIVATE echo_options
)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(
        cover_build.c
        cue_sheet.c
        library_builder_paths.c
        library_scan.c
        library_scan_chapters.c
        library_scan_cue.c
        mp4_chapters.c
        playlist_reader.c
        replay_gain.c
        tag_duration.c
        tags_ape.c
        tags_asf.c
        tags_genres.c
        tags_id3.c
        tags_mp4.c
        tags_mpeg.c
        tags_picture.c
        tags_riff.c
        tags_vorbis.c
        track_fields.c
        track_tags.c
        TARGET_DIRECTORY echo_library_scan
        PROPERTIES COMPILE_OPTIONS -Os
    )
endif()
