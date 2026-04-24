idf_component_register(
    SRCS "src/classifier.c"
         "src/mock_classifier.c"
         "src/model_classifier_stub.c"
         "src/model_classifier_espdl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common log esp_timer esp-dl
    EMBED_FILES "model/student96_w1_s3_ptq.espdl"
)
