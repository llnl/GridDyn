function v = griddyn_object_not_initialized()
  persistent vInitialized;
  if isempty(vInitialized)
    vInitialized = griddynMEX(0, 9);
  end
  v = vInitialized;
end
